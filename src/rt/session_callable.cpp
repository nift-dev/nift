#include "rt/session_callable.h"
#include "rt/aggregate.h"
#include "rt/cell.h"
#include "rt/proto.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
// rt-lint: cold-file immutable callable facts; actual Values stay in session cells
namespace nift::rt {
namespace {
using Shape = std::shared_ptr<const SessionCallableShape>;
Shape incomplete() {
    auto out = std::make_shared<SessionCallableShape>(); out->complete = false; return out;
}
Shape merge(const Shape& a, const Shape& b) {
    if (!a) return b;
    if (!b || a == b) return a;
    auto out = std::make_shared<SessionCallableShape>(*a);
    out->complete = a->complete && b->complete;
    for (const auto& target : b->targets)
        if (std::none_of(out->targets.begin(), out->targets.end(),
                [&](const auto& item) { return item.identity == target.identity; })) out->targets.push_back(target);
    for (const auto& field : b->fields) out->fields[field.first] = merge(out->fields[field.first], field.second);
    out->element = merge(a->element, b->element);
    return out;
}
struct Scan {
    std::unordered_map<const HeapObject*, Shape> memo;
    std::unordered_set<const HeapObject*> active;
    Shape visit(const Value& value, unsigned depth) {
        if (!may_contain_identity(value)) return {};
        if (depth > 64) return incomplete();
        if (!(value.is(Kind::Function) || value.is(Kind::Array) || value.is(Kind::Object))) return incomplete();
        if (active.count(value.heap())) return incomplete();  // no shared_ptr cycles in compiler facts
        const auto found = memo.find(value.heap());
        if (found != memo.end()) return found->second;
        active.insert(value.heap());
        auto out = std::make_shared<SessionCallableShape>();
        if (value.is(Kind::Function)) {
            const auto* function = value.heap_as<FunctionObject>();
            const auto* proto = static_cast<const Proto*>(function->code);
            if (function->session_identity && proto && proto->session_definition) {
                SessionCallable target{function->session_identity, proto->session_definition, {}};
                for (const auto& capture : function->upvalues) {
                    const auto* cell = is_cell(capture) ? cell_of(capture) : nullptr;
                    // Never resolve a selector or read a cached location just to compile metadata.
                    auto shape = cell && cell->binding_type >= 0 ? visit(cell->value, depth + 1) : incomplete();
                    if (shape && !shape->complete) out->complete = false;
                    target.captures.push_back(std::move(shape));
                }
                out->targets.push_back(std::move(target));
            } else out->complete = false;
        } else if (value.is(Kind::Array)) {
            for (const auto& item : array_items(value)) out->element = merge(out->element, visit(item, depth + 1));
            if (out->element && !out->element->complete) out->complete = false;
        } else {
            for (const auto& item : object_entries(value)) {
                const auto child = visit(item.second, depth + 1);
                if (child) {
                    out->complete = out->complete && child->complete;
                    out->fields.emplace(std::string(atom_text(item.first)), child);
                }
            }
        }
        active.erase(value.heap());
        Shape result = out;
        if (out->complete && out->targets.empty() && out->fields.empty() && !out->element) result.reset();
        memo.emplace(value.heap(), result);
        return result;
    }
};
} // namespace
Shape merge_session_callable_shapes(const Shape& a, const Shape& b) { return merge(a, b); }
Shape session_callable_shape(const Value& value) { return Scan{}.visit(value, 0); }
std::vector<SessionCallable> session_callable_targets(const Shape& shape) {
    std::vector<SessionCallable> out;
    std::vector<Shape> pending{shape};
    std::unordered_set<const SessionCallableShape*> seen;
    while (!pending.empty()) {
        const auto item = pending.back(); pending.pop_back();
        if (!item || !seen.insert(item.get()).second) continue;
        for (const auto& target : item->targets) {
            if (std::none_of(out.begin(), out.end(), [&](const auto& old) { return old.identity == target.identity; }))
                out.push_back(target);
            for (const auto& capture : target.captures) pending.push_back(capture);
        }
        pending.push_back(item->element);
        for (const auto& field : item->fields) pending.push_back(field.second);
    }
    return out;
}
} // namespace nift::rt
