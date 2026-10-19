#pragma once
// The D-040 cycle rule (semantics.md V16) for stores that could close a cycle
// through identity-bearing values: closure-captured variables and resource
// payloads. Ordinary stores never call it: a store whose value fails the O(1)
// rt::may_contain_identity test cannot close a cycle, and ordinary values are
// acyclic by construction (copy-on-write plus retain-before-unique).
//
// Model. A store writes value V into a *target*: a captured variable (a cell,
// identified by an opaque pointer) or a resource's payload (identified by its
// handle). It closes a cycle exactly when the target is reachable from V
// through:
//   * aggregates (array elements, object entries, struct fields) whose
//     identity summary bit is set (an aggregate with the bit clear holds no
//     callable or handle anywhere below it, so it is never entered);
//   * a callable's captured variables (CycleGraph::captured), then their
//     contents;
//   * a handle's payload (CycleGraph::payload), then its contents.
// The walk is iterative with a visited set, so its cost is bounded by the
// identity-bearing subgraph reachable from V, never by plain data. While
// walking, an aggregate whose direct children turn out to hold no callable,
// no handle and no marked aggregate gets its summary bit cleared (the bit is
// conservative; see rt/heap.h), so later checks skip it.
//
// The hooks are the integration points: the VM supplies the closure-cell
// layout (CP12C) and the CP17/CP22 resource payloads. The default treats each
// FunctionObject upvalue slot as a cell identified by its address and gives
// handles no payload.
#include "rt/aggregate.h"
#include "rt/heap.h"
#include "rt/value.h"

#include <cstddef>
#include <cstdint>

namespace nift::rt {

struct CycleTarget {
    enum class Kind : std::uint8_t { Cell, Resource } kind = Kind::Cell;
    const void* cell = nullptr;  // Kind::Cell
    Value handle;                // Kind::Resource
    static CycleTarget captured_cell(const void* cell_identity) {
        CycleTarget t; t.kind = Kind::Cell; t.cell = cell_identity; return t;
    }
    static CycleTarget resource(Value handle_value) {
        CycleTarget t; t.kind = Kind::Resource; t.handle = static_cast<Value&&>(handle_value); return t;
    }
};

// Receives each captured variable: its cell identity and current contents.
// `contents` must stay valid (and unmodified) until the check returns.
class CaptureVisitor {
public:
    virtual void cell(const void* identity, const Value& contents) = 0;
protected:
    ~CaptureVisitor() = default;
};

class CycleGraph {
public:
    virtual ~CycleGraph() = default;
    // Calls visitor.cell(...) for each variable the callable captures.
    virtual void captured(const FunctionObject& function, CaptureVisitor& visitor) const;
    // The payload held by the resource `handle` refers to, or null.
    virtual const Value* payload(const Value& handle) const;
};

// True when storing `stored` into `target` would make a value contain itself.
// Callers raise runtime.reference_cycle ("reference cycle: the stored value
// would contain itself") before writing anything.
// The O(1) test is inline, so a store of plain data costs one tag/bit test
// and no call; only values that may contain callables or handles walk.
bool would_close_cycle_walk(const CycleTarget& target, const Value& stored, const CycleGraph& graph);
inline bool would_close_cycle(const CycleTarget& target, const Value& stored, const CycleGraph& graph) {
#ifdef NIFT_RT_NO_CYCLE_CHECK  // measurement build only (bench-rt-value-cycle): the check compiled out
    (void)target; (void)stored; (void)graph;
    return false;
#else
    return may_contain_identity(stored) && would_close_cycle_walk(target, stored, graph);
#endif
}

// Counters for tests and the V16 cost measurement (per thread).
struct CycleCheckStats {
    std::uint64_t checks = 0;           // stores that walked (failed the O(1) test)
    std::uint64_t nodes_visited = 0;    // values visited while walking
    std::uint64_t summaries_cleared = 0;
};
CycleCheckStats& cycle_check_stats() noexcept;

} // namespace nift::rt
