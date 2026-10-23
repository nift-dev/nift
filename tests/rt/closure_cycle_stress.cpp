// CP12C Slice 1 C1-A stress: one million rejected cyclic cell stores under
// reference counting. Each attempt must refuse, leave the cell unchanged and
// release every allocation (live_heap_bytes returns to its baseline).
#include "rt/cell.h"
#include "rt/heap_account.h"
#include "rt/value_cycle.h"

#include <cstdio>

using namespace nift::rt;

int main() {
    const std::size_t baseline = live_heap_bytes();
    CycleGraph graph;
    std::size_t peak = baseline;
    for (int i = 0; i < 1000000; ++i) {
        {
            Value cell = make_cell(Value::integer(7), -1);
            auto* function = new FunctionObject();
            function->is_lambda = true;
            function->upvalues.push_back(cell);
            heap_retally(function);
            Value closure = Value::adopt(function);
            const std::size_t live = live_heap_bytes();
            if (live > peak) peak = live;
            const bool refused = would_close_cycle(CycleTarget::captured_cell(cell.heap()), closure, graph);
            if (!refused) {
                std::printf("FAIL attempt %d: cyclic store admitted\n", i);
                return 1;
            }
            if (cell_load(cell).as_int() != 7) {
                std::printf("FAIL attempt %d: refused store changed the cell\n", i);
                return 1;
            }
        }
        if (live_heap_bytes() != baseline) {
            std::printf("FAIL attempt %d: live heap %zu != baseline %zu (leak)\n", i, live_heap_bytes(), baseline);
            return 1;
        }
    }
    // Plain data takes the O(1) gate and never walks the cycle graph.
    const std::uint64_t checks_before = cycle_check_stats().checks;
    Value cell = make_cell(Value::integer(0), -1);
    if (would_close_cycle(CycleTarget::captured_cell(cell.heap()), Value::integer(5), graph)) {
        std::printf("FAIL plain-data store was refused\n");
        return 1;
    }
    if (cycle_check_stats().checks != checks_before) {
        std::printf("FAIL plain-data store walked the cycle graph\n");
        return 1;
    }
    std::printf("PASS closure cycle stress: 1000000 refused stores, peak %zu, baseline %zu\n", peak, baseline);
    return 0;
}
