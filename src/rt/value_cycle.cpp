#include "rt/value_cycle.h"

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
        if (!may_contain_identity(contents) || !b_.seen.insert(address_key(identity))) return;
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
                if (may_contain_identity(c->value) && b_.seen.insert(address_key(c))) b_.work.push_back(&c->value);
                return;
            }
            case Kind::Handle: visit_handle(v); return;
            case Kind::Array: case Kind::Object: case Kind::Struct: visit_aggregate(v); return;
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
        if (p && may_contain_identity(*p)) b_.work.push_back(p);
    }

    void consider(const Value& child, bool& any) {
        if (!may_contain_identity(child)) return;
        any = true;
        b_.work.push_back(&child);
    }

    void visit_aggregate(const Value& v) {
        HeapObject* object = v.heap();
        if (!may_hold_identity(object) || !b_.seen.insert(address_key(object))) return;
        bool any = false;
        if (v.is(Kind::Array)) {
            for (const Value& child : array_items(v)) consider(child, any);
        } else if (v.is(Kind::Object)) {
            for (const auto& entry : object_entries(v)) consider(entry.second, any);
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
};

} // namespace

bool would_close_cycle_walk(const CycleTarget& target, const Value& stored, const CycleGraph& graph) {
    ++cycle_check_stats().checks;
    thread_local Buffers shared;
    if (shared.busy) {
        Buffers own;
        return Walk(target, graph, own).run(stored);
    }
    shared.busy = true;
    const bool closes = Walk(target, graph, shared).run(stored);
    shared.busy = false;
    return closes;
}

} // namespace nift::rt
