#pragma once
// B2 (b2-refrep.md section 16, owner requirement E): the record of one running
// in-place comparator sort's protection, kept by its Vm (Vm::sort_protection_;
// the functions are in vm_collection_sort.cpp). The receiver's header bit
// (kSortProtected, rt/heap.h) is set when the sort starts and released when it
// completes (op_collection_sort, also after a drain), by the sweep at the end
// of an unwind (every controller that is no longer active), and at every exit
// of Vm::run (a stop, the end of a session statement, an exception, a parallel
// task's isolate run) and in ~Vm. A cooperative task never holds one across
// its teardown: such a program is refused (coro-collection-callback).
#include "rt/value.h"

#include <cstddef>
#include <cstdint>

namespace nift::rt {

struct SortProtection {
    Value receiver;
    std::uint32_t task = 0;               // the coroutine task that runs the sort (0: main)
    std::size_t slot = 0, site = 0;       // the controller's registers (stack index) and call site
};

} // namespace nift::rt
