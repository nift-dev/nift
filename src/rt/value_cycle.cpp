#include "rt/value_cycle.h"
#include "rt/thread_owned.h"

#include "rt/aggregate.h"
#include "rt/cell.h"
#include "rt/compare.h"

#include <algorithm>
#include <vector>

namespace nift::rt {

void CycleGraph::captured(const FunctionObject& function, CaptureVisitor& visitor) const {
    for (const Value& slot : function.upvalues) {
        if (const CellObject* cell = cell_of(slot)) visitor.cell(cell, cell->value);
        else visitor.cell(&slot, slot);  // test/legacy graph without dedicated cells
    }
}

const Value* CycleGraph::payload(const Value&) const { return nullptr; }

CycleCheckStats& cycle_check_stats() noexcept {
    thread_local CycleCheckStats stats;
    return stats;
}

namespace {

// Visited identities: callables, aggregates and cells by address, handles by
// a key folded from (kind, id, generation). A walk usually meets a handful,
// so a linear scan of a reused buffer is cheapest; past kSorted entries the
// buffer is kept sorted and searched by bisection.
class Visited {
public:
    void clear() noexcept { keys_.clear(); }
    // True when `key` was not seen before (and records it).
    bool insert(std::uintptr_t key) {
        if (keys_.size() < kSorted) {
            if (std::find(keys_.begin(), keys_.end(), key) != keys_.end()) return false;
            keys_.push_back(key);
            if (keys_.size() == kSorted) std::sort(keys_.begin(), keys_.end());
            return true;
        }
        auto at = std::lower_bound(keys_.begin(), keys_.end(), key);
        if (at != keys_.end() && *at == key) return false;
        keys_.insert(at, key);
        return true;
    }

private:
    static constexpr std::size_t kSorted = 32;
    std::vector<std::uintptr_t> keys_;
};

std::uintptr_t handle_key(const Value& h) {
    // Odd values never collide with object addresses (aligned to 8).
    const std::uint64_t k = (h.handle_id() * 0x9e3779b97f4a7c15ull) ^ (std::uint64_t{h.handle_generation()} << 8) ^
                            static_cast<std::uint64_t>(h.handle_kind());
    return static_cast<std::uintptr_t>(k | 1u);
}

std::uintptr_t address_key(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

// One walk. Buffers are thread-local and reused (no allocation in steady
// state); a nested check (a graph hook that itself checks) gets its own.
struct Buffers {
    std::vector<const Value*> work;
    Visited seen;
    bool busy = false;
};

class Walk final : public CaptureVisitor {
public:
    Walk(const CycleTarget& target, const CycleGraph& graph, Buffers& buffers)
        : target_(target), graph_(graph), b_(buffers) {
        b_.work.clear();
        b_.seen.clear();
    }

    bool run(const Value& stored) {
        b_.work.push_back(&stored);
        while (!b_.work.empty() && !found_) {
            const Value* v = b_.work.back();
            b_.work.pop_back();
            ++cycle_check_stats().nodes_visited;
            visit(*v);
        }
        return found_;
    }

    void cell(const void* identity, const Value& contents) override {
        if (found_) return;
        if (target_.kind == CycleTarget::Kind::Cell && identity == target_.cell) { found_ = true; return; }
        if (!relevant(contents) || !b_.seen.insert(address_key(identity))) return;
        b_.work.push_back(&contents);
    }

private:
    void visit(const Value& v) {
        switch (v.kind()) {
            case Kind::Function: {
                const auto* f = v.heap_as<FunctionObject>();
                if (b_.seen.insert(address_key(f))) graph_.captured(*f, *this);
                return;
            }
            case Kind::Cell: {
                const auto* c = v.heap_as<CellObject>();
                if (target_.kind == CycleTarget::Kind::Cell && target_.cell == c) { found_ = true; return; }
                if (relevant(c->value) && b_.seen.insert(address_key(c))) b_.work.push_back(&c->value);
                return;
            }
            case Kind::Handle: visit_handle(v); return;
            case Kind::Array: case Kind::Object: case Kind::Struct: case Kind::Collection: visit_aggregate(v); return;
            default: return;
        }
    }

    void visit_handle(const Value& h) {
        if (target_.kind == CycleTarget::Kind::Resource && same(h, target_.handle) == Sameness::Same) {
            found_ = true;
            return;
        }
        if (!b_.seen.insert(handle_key(h))) return;
        const Value* p = graph_.payload(h);
        if (p && relevant(*p)) b_.work.push_back(p);
    }

    // B2 (plan 5.1): the summary bit stays set while any direct child is a
    // reference (an array or object included), whatever the target; the child
    // is walked when it may hold the target.
    void consider(const Value& child, bool& any) {
        const bool reference = child.is(Kind::Array) || child.is(Kind::Object) || may_contain_identity(child);
        if (!reference) return;
        any = true;
        if (relevant(child)) b_.work.push_back(&child);
    }

    // A value worth visiting: it may hold an identity-bearing value, or (an
    // aggregate target) it is an array or object, which may itself be the
    // target even when its own summary bit is clear.
    bool relevant(const Value& v) const noexcept {
        return may_contain_identity(v) || (aggregate_target_ && (v.is(Kind::Array) || v.is(Kind::Object)));
    }

    void visit_aggregate(const Value& v) {
        HeapObject* object = v.heap();
#if !(defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 1)
        // D-NEXT-refsem: the target itself, before the summary shortcut (a
        // collection or instance holding only plain data may still be it).
        if (target_.kind == CycleTarget::Kind::Aggregate && target_.cell == object) { found_ = true; return; }
#endif
        if (!may_hold_identity(object) || !b_.seen.insert(address_key(object))) return;
        bool any = false;
        if (v.is(Kind::Array)) {
            for (const Value& child : array_items(v)) consider(child, any);
        } else if (v.is(Kind::Object)) {
            for (const auto& entry : object_entries(v)) consider(entry.second, any);
        } else if (v.is(Kind::Collection)) {  // D-063: keys, values, members, elements
            for (const Value& child : collection_items(v)) consider(child, any);
        } else {
            for (const auto& field : struct_fields(v)) consider(field.second, any);
        }
        if (!any) {  // verified: nothing identity-bearing below (the bit is conservative)
            clear_may_hold_identity(object);
            ++cycle_check_stats().summaries_cleared;
        }
    }

    const CycleTarget& target_;
    const CycleGraph& graph_;
    Buffers& b_;
    bool found_ = false;
    // B2: an array or object target may be any array or object, including one
    // whose own summary bit is clear (flat data); the summary prunes its
    // children. (A struct or collection target is never a flat array.)
    const bool aggregate_target_ = target_.kind == CycleTarget::Kind::Aggregate &&
                                   (static_cast<const HeapObject*>(target_.cell)->kind() == Kind::Array ||
                                    static_cast<const HeapObject*>(target_.cell)->kind() == Kind::Object);
};

} // namespace

bool legacy_reference_cycle(const HeapObject* receiver, const Value& stored) {
    const auto reference = [](const Value& v) { return v.is(Kind::Struct) || v.is(Kind::Collection); };
    if (!reference(stored)) return false;
    std::vector<const HeapObject*> work{stored.heap()}, seen;
    while (!work.empty()) {
        const HeapObject* object = work.back();
        work.pop_back();
        if (object == receiver) return true;
        if (std::find(seen.begin(), seen.end(), object) != seen.end()) continue;
        seen.push_back(object);
        if (object->kind() == Kind::Struct) {
            for (const auto& field : static_cast<const StructObject*>(object)->fields)
                if (reference(field.second)) work.push_back(field.second.heap());
            continue;
        }
        const auto* c = static_cast<const CollectionObject*>(object);
        const std::size_t stride = c->sub == CollectionKind::Map ? 2 : 1;  // map keys are not followed (legacy)
        for (std::size_t k = stride - 1; k < c->items.size(); k += stride)
            if (reference(c->items[k])) work.push_back(c->items[k].heap());
    }
    return false;
}

bool would_close_cycle_walk(const CycleTarget& target, const Value& stored, const CycleGraph& graph) {
    ++cycle_check_stats().checks;
    Buffers& shared = nift::tls::owned<Buffers>();  // B03: no TLS destructor registration (rt/thread_owned.h)
    if (shared.busy) {
        Buffers own;
        return Walk(target, graph, own).run(stored);
    }
    shared.busy = true;
    struct WalkScope {
        Buffers& buffers;
        ~WalkScope() { buffers.work.clear(); buffers.busy = false; }
    } scope{shared};
    return Walk(target, graph, shared).run(stored);
}

} // namespace nift::rt
