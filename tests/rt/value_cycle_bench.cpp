// Cost of the D-040 cycle check (V16) isolated from the store itself.
//
//   value_cycle_bench [iterations]
//
// Each workload is a store into a captured cell, as the VM will do it (2.7b):
// `if (would_close_cycle(cell, v)) error; cell = v;`. The binary is built
// twice by bench-rt-value-cycle, with the check and with
// -DNIFT_RT_NO_CYCLE_CHECK; the guard is on the difference per store, not on
// the ratio to plain stores (closure and handle stores have their own costs).
// Output: one line per workload, `name ns_per_store`.
#include "rt/aggregate.h"
#include "rt/value_cycle.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace nift::rt;

namespace {

double per_store(const char* name, const Value& stored, long iterations) {
    CycleGraph graph;
    Value cell;
    const CycleTarget target = CycleTarget::captured_cell(&cell);
    long refused = 0;
    double best = 1e30;
    for (int round = 0; round < 5; ++round) {
        const auto start = std::chrono::steady_clock::now();
        for (long n = 0; n < iterations; ++n) {
            if (would_close_cycle(target, stored, graph)) { ++refused; continue; }
            cell = stored;
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
        if (ns / static_cast<double>(iterations) < best) best = ns / static_cast<double>(iterations);
    }
    std::printf("%s %.2f\n", name, best);
    return refused;
}

} // namespace

int main(int argc, char** argv) {
    const long iterations = argc > 1 ? std::strtol(argv[1], nullptr, 10) : 2000000;
    Value plain = make_array();
    for (int n = 0; n < 8; ++n) array_push(plain, Value::integer(n));
    auto* f = new FunctionObject();  // a closure over two variables holding plain data
    f->upvalues.push_back(Value::integer(1));
    f->upvalues.push_back(plain);
    Value closure = Value::adopt(f);
    Value handle = Value::handle(HandleKind::Mutex, 3, 1);
    Value holder = make_array();  // [1, closure, handle] two levels below a struct
    array_push(holder, Value::integer(1));
    array_push(holder, closure);
    array_push(holder, handle);
    Value nested = make_struct(atom("Box"));
    struct_add_field(nested, atom("items"), holder);
    long refused = 0;
    refused += static_cast<long>(per_store("store_plain", plain, iterations));
    refused += static_cast<long>(per_store("store_callable", closure, iterations));
    refused += static_cast<long>(per_store("store_handle", handle, iterations));
    refused += static_cast<long>(per_store("store_nested_identity", nested, iterations));
    return refused == 0 ? 0 : 1;
}
