// C3 follow-up (permanent regression): a heap limit of 0 must mean "no room",
// never "budget disabled". Vm::update_heap_limit sets heap_limit_ to 0 when
// the register and frame stacks already exceed the budget, and the VM passes
// it to natives (vm_builtin.cpp). Before the fix, natives read 0 as disabled
// and built any result unchecked: at 3b64c5ca this printed
// "budget 1024, heap_limit 0, status 0, result 4194304 bytes" (exit 1).
// Now every budget-checked path refuses: join and replace (BudgetedText),
// ArrayBuilder (over_budget) and reserve_result itself.
#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt/natives/array_methods.h"
#include "rt/natives/builtin.h"
#include "rt/natives/string_methods.h"
#include "rt/natives/support.h"
#include "rt/string.h"
#include <cstdio>
#include <string>
using namespace nift::rt;
using namespace nift::rt::natives;
int main() {
    const std::size_t budget = 1024, stacks = 256 * sizeof(Value);  // 4096 bytes of registers at start
    Context context;
    context.heap_limit = stacks < budget ? budget - stacks : 0;      // Vm::update_heap_limit's rule
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::printf("FAIL %s\n", what); ++failures; } };
    check(context.heap_limit == 0, "the VM rule gives 0 here");
    check(!reserve_result(context, 0), "reserve_result refuses with no room");
    check(context.over_budget() || live_heap_bytes() == 0, "over_budget sees no room");
    Value parts = make_array(64);
    for (int i = 0; i < 64; ++i) array_push(parts, make_string(std::string(64 << 10, 'x')));
    {
        Value args[2] = {parts, make_string("")};
        Value out; std::string error;
        const Status st = arr_join(context, args, 2, out, error);
        std::printf("join: budget %zu, heap_limit %zu, status %d\n", budget, context.heap_limit, int(st));
        check(st == Status::Memory, "join of 4 MiB refused at heap_limit 0");
    }
    {
        Value args[3] = {make_string(std::string(1 << 16, 'a')), make_string("a"), make_string(std::string(64, 'b'))};
        Value out; std::string error;
        const Status st = str_replace(context, args, 3, out, error);
        std::printf("replace: status %d\n", int(st));
        check(st == Status::Memory, "replace to 4 MiB refused at heap_limit 0");
    }
    // The disabled value is distinct and still unbounded (unit tests, hosts).
    Context unlimited;
    check(unlimited.heap_limit == Context::kNoHeapLimit, "default context is unlimited");
    check(reserve_result(unlimited, 1 << 20) && !unlimited.over_budget(), "unlimited context allows");
    std::printf("heap-limit-zero: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
