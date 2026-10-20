// heap_limit == 0 bypass (C3 follow-up): Vm::update_heap_limit sets heap_limit_
// to 0 when the register and frame stacks exceed the budget, and natives read
// heap_limit == 0 as "budget disabled", so a native then builds any result
// unchecked.
#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt/natives/array_methods.h"
#include "rt/string.h"
#include <cstdio>
#include <string>
using namespace nift::rt;
using namespace nift::rt::natives;
int main() {
    const std::size_t budget = 1024, stacks = 256 * sizeof(Value);  // 4096 bytes of registers at start
    Context context;
    context.heap_limit = stacks < budget ? budget - stacks : 0;      // Vm::update_heap_limit's rule
    Value parts = make_array(64);
    for (int i = 0; i < 64; ++i) array_push(parts, make_string(std::string(64 << 10, 'x')));
    Value args[2] = {parts, make_string("")};
    Value out; std::string error;
    const Status st = arr_join(context, args, 2, out, error);
    std::printf("budget %zu, heap_limit %zu, status %d, result %zu bytes\n", budget, context.heap_limit, int(st),
                st == Status::Ok ? string_view_of(out).size() : 0);
    return st == Status::Ok ? 1 : 0;  // 1: the 4 MiB result was built (bypass)
}
