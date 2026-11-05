#include "rt/copy_graph.h"
#include "rt/mutation.h"

#include "rt/aggregate.h"
#include "rt/heap.h"
#include "rt/heap_account.h"

#include <unordered_map>
#include <vector>

// rt-lint: cold-file copy()/deepcopy() graph copies (D-NEXT-refsem #3)

namespace nift::rt {

namespace {

bool copied(const Value& v) {
    return v.is(Kind::Array) || v.is(Kind::Object) || v.is(Kind::Struct) || v.is(Kind::Collection);
}

// An upper bound of the bytes a copy of `object` is counted with (the object,
// its storage, an index; granule rounding), before it exists.
std::size_t estimate(const HeapObject* object) {
    return heap_footprint(object) + 2 * kHeapGranule;
}

bool fits(std::size_t bytes, std::size_t heap_limit) {
    if (heap_limit == kNoCopyLimit) return true;
    const std::size_t live = live_heap_bytes();
    return live <= heap_limit && bytes <= heap_limit - live;
}

template <class F> void each_child(const Value& v, F&& f) {
    if (v.is(Kind::Array)) {
        for (const Value& item : array_items(v)) f(item);
    } else if (v.is(Kind::Object)) {
        for (const auto& entry : object_entries(v)) f(entry.second);
    } else if (v.is(Kind::Struct)) {
        for (const auto& field : struct_fields(v)) f(field.second);
    } else if (v.is(Kind::Collection)) {
        for (const Value& item : collection_items(v)) f(item);
    }
}

// B3: a copy keeps the source instance's descriptor (its privacy included).
std::shared_ptr<const StructType> copy_descriptor(const Value& instance) {
#if defined(NIFT_B3_MUTANT) && NIFT_B3_MUTANT == 4
    const StructType& type = struct_type(instance);  // D4: the copy's descriptor is all public
    return StructType::make(type.name, type.fields, {});
#else
    return struct_type_ref(instance);
#endif
}

// A new empty object of the source's kind (the shell, linked in phase 2).
Value shell(const Value& v) {
    if (v.is(Kind::Array)) return make_array(array_size(v));
    if (v.is(Kind::Object)) return make_object();
    if (v.is(Kind::Struct)) return make_struct(copy_descriptor(v), struct_fields(v).size());
    return make_collection(collection_kind(v));
}

// Tooling 2 (in place, tooling.md 2.4): the shells are created first (so every
// node of a DAG maps to one copy) and filled in post-order afterwards, each
// before any parent holds it; a shell is not a builder's value because the
// identity map (copies_) holds it from creation, so the fill takes the key of
// deep_copy_tuple, the owner.
class GraphCopy {
public:
    explicit GraphCopy(const build::InPlaceKey& key) : key_(key) {}
    // Phase 1: every reachable object once (iterative; no recursion depth),
    // its copied children, and the budget for all of them before anything is
    // allocated.
    bool discover(const Value* roots, std::size_t count, std::size_t heap_limit) {
        std::vector<std::size_t> work;
        std::size_t bytes = 0;
        const auto visit = [&](const Value* v) {
            const auto found = index_.find(v->heap());
            if (found != index_.end()) return found->second;
            const std::size_t at = sources_.size();
            index_.emplace(v->heap(), at);
            sources_.push_back(v);
            children_.emplace_back();
            bytes += estimate(v->heap());
            work.push_back(at);
            return at;
        };
        for (std::size_t k = 0; k < count; ++k)
            if (copied(roots[k])) visit(&roots[k]);
        while (!work.empty()) {
            const std::size_t at = work.back();
            work.pop_back();
            each_child(*sources_[at], [&](const Value& child) {
                if (!copied(child)) return;
                const std::size_t c = visit(&child);
                children_[at].push_back(c);
            });
        }
        return fits(bytes, heap_limit);
    }

    // Phase 2: the shells (each owned by a Value: a failed allocation frees
    // every one made so far), then each shell's contents, mapped, children
    // before parents (each copy is complete, and marked immutable when asked,
    // before any parent holds it). The graph is acyclic (reference cycles are
    // refused), so the post-order exists.
    void link(bool keep_immutable) {
        copies_.reserve(sources_.size());
        for (const Value* source : sources_) copies_.push_back(shell(*source));
        std::vector<std::uint8_t> state(sources_.size(), 0);  // 0 new, 1 open, 2 filled
        std::vector<std::pair<std::size_t, std::size_t>> stack;  // (node, next child)
        for (std::size_t start = 0; start < sources_.size(); ++start) {
            if (state[start]) continue;
            state[start] = 1;
            stack.push_back({start, 0});
            while (!stack.empty()) {
                const std::size_t node = stack.back().first;
                if (stack.back().second < children_[node].size()) {
                    const std::size_t child = children_[node][stack.back().second++];
                    if (state[child] == 0) {
                        state[child] = 1;
                        stack.push_back({child, 0});
                    }
                    continue;
                }
                fill(*sources_[node], copies_[node]);
                if (keep_immutable && is_immutable(sources_[node]->heap())) mark_immutable(copies_[node].heap());
                state[node] = 2;
                stack.pop_back();
            }
        }
    }

    Value mapped(const Value& v) const {
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 9
        // R9: aliasing lost (every reference gets its own copy; one level only)
        if (copied(v)) { Value fresh = shell(v); fill_shallow(v, fresh); return fresh; }
#endif
        return copied(v) ? copies_[index_.at(v.heap())] : v;
    }

private:
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 9
    void fill_shallow(const Value& source, Value& copy) const {
        if (source.is(Kind::Collection)) { copy = collection_shallow_clone(source); return; }
        if (source.is(Kind::Struct)) {
            // rt-lint: storage-ok fresh, the new copy
            for (const auto& f : struct_fields(source)) Construction::struct_add_field(key_, copy, f.first, f.second);
            return;
        }
        copy = source;
    }
#endif
    void fill(const Value& source, Value& copy) const {
        if (source.is(Kind::Array)) {
            // rt-lint: storage-ok fresh, the new copy
            for (const Value& item : array_items(source)) Construction::array_push(key_, copy, mapped(item));
        } else if (source.is(Kind::Object)) {
            for (const auto& entry : object_entries(source))
                // rt-lint: storage-ok fresh, the new copy
                Construction::object_set(key_, copy, entry.first, mapped(entry.second));
        } else if (source.is(Kind::Struct)) {
            for (const auto& field : struct_fields(source))
                // rt-lint: storage-ok fresh, the new copy
                Construction::struct_add_field(key_, copy, field.first, mapped(field.second));
        } else {
            const auto& items = collection_items(source);
            const CollectionKind kind = collection_kind(source);
            // Keys and set members are scalars or callables (never copied):
            // inserted in order, the index is rebuilt as the source's.
            if (kind == CollectionKind::Map) {
                for (std::size_t k = 0; k + 1 < items.size(); k += 2)
                    // rt-lint: storage-ok fresh, fresh copy
                    Construction::map_set(key_, copy, items[k], mapped(items[k + 1]));
            } else if (kind == CollectionKind::Set) {
                // rt-lint: storage-ok fresh, fresh copy
                for (const Value& member : items) Construction::set_add(key_, copy, mapped(member));
            } else {
                // rt-lint: storage-ok fresh, fresh copy
                for (const Value& item : items) Construction::stack_push(key_, copy, mapped(item));
            }
        }
    }

    const build::InPlaceKey& key_;
    std::vector<const Value*> sources_;
    std::vector<std::vector<std::size_t>> children_;
    std::unordered_map<const HeapObject*, std::size_t> index_;
    std::vector<Value> copies_;
};

} // namespace

CopyStatus shallow_copy(const Value& source, Value& out, std::size_t heap_limit) {
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 10
    return deep_copy(source, out, heap_limit);  // R10: copy() is deep (the v4.4 rule)
#endif
    if (source.is(Kind::Collection)) {
        if (!fits(estimate(source.heap()), heap_limit)) return CopyStatus::Budget;
        out = collection_shallow_clone(source);
        return CopyStatus::Ok;
    }
    if (source.is(Kind::Struct)) {
        if (!fits(estimate(source.heap()), heap_limit)) return CopyStatus::Budget;
        build::Struct copy(copy_descriptor(source), struct_fields(source).size());
        for (const auto& field : struct_fields(source)) copy.add(field.first, field.second);
        out = std::move(copy).publish();
        return CopyStatus::Ok;
    }
    if (source.is(Kind::Array) || source.is(Kind::Object)) {
        // B2: arrays and objects are references, so copy() builds a new,
        // mutable outer array or object sharing the contents (B1 did this only
        // for an immutable one; the rest were copy-on-write values).
        if (!fits(estimate(source.heap()), heap_limit)) return CopyStatus::Budget;
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 25
        out = source;  // R-B4: copy() returns the same object
        return CopyStatus::Ok;
#endif
        if (source.is(Kind::Array)) {
            build::Array copy(array_size(source));
            for (const Value& item : array_view(source)) copy.push(item);
            out = std::move(copy).publish();
        } else {
            build::Object copy;
            for (const auto& entry : object_view(source)) copy.set(entry.first, entry.second);
            out = std::move(copy).publish();
        }
        return CopyStatus::Ok;
    }
    out = source;  // scalars, strings, identities
    return CopyStatus::Ok;
}

CopyStatus deep_copy_tuple(const Value* values, std::size_t count, Value* out, std::size_t heap_limit,
                           bool keep_immutable) {
    const build::InPlaceKey key;  // tooling.md 2.4: the post-order fill
    GraphCopy graph(key);
    if (!graph.discover(values, count, heap_limit)) return CopyStatus::Budget;
    graph.link(keep_immutable);
    for (std::size_t k = 0; k < count; ++k) out[k] = graph.mapped(values[k]);
    return CopyStatus::Ok;
}

namespace {
std::size_t constant_bytes(const Value& v) {
    if (v.is(Kind::Array)) {
        std::size_t bytes = estimate(v.heap());
        for (const Value& item : array_view(v)) bytes += constant_bytes(item);
        return bytes;
    }
    if (v.is(Kind::Object)) {
        std::size_t bytes = estimate(v.heap());
        for (const auto& entry : object_view(v)) bytes += constant_bytes(entry.second);
        return bytes;
    }
    return 0;
}
Value clone_tree(const Value& v) {
    if (v.is(Kind::Array)) {
        const auto& items = array_view(v);
        build::Array copy(items.size());
        for (const Value& item : items) copy.push(clone_tree(item));
        return std::move(copy).publish();
    }
    if (v.is(Kind::Object)) {
        build::Object copy;
        for (const auto& entry : object_view(v)) copy.set(entry.first, clone_tree(entry.second));
        return std::move(copy).publish();
    }
    return v;  // a scalar or a string: immutable, shared
}
} // namespace

CopyStatus clone_constant(const Value& source, Value& out, std::size_t heap_limit) {
    if (!fits(constant_bytes(source), heap_limit)) return CopyStatus::Budget;
    out = clone_tree(source);
    return CopyStatus::Ok;
}

CopyStatus deep_copy(const Value& source, Value& out, std::size_t heap_limit) {
    return deep_copy_tuple(&source, 1, &out, heap_limit);
}

} // namespace nift::rt
