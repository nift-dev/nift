// CP23 snapshot constants (owner, 2026-10-30): snapshot_isolate_program builds
// its unpublished constants array in one exactly reserved pass. Its encoded
// graph must equal the graph that per-element array_push construction gives:
// the same constants in the same order with the same kinds and values, the
// same per-proto counts and wires, and live heap accounting back at its
// baseline afterwards. Covers literal kinds, function constants, a deep chain
// of nested snapshots (the admission unit's recursive-await shape) and
// constant-free programs.
#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/isolate_task.h"
#include "rt/parallel_task.h"
#include "rt_test.h"
#include "rt_vm_tool.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace nift::rt;

namespace {

bool same_constant(const Value& a, const Value& b) {
    if (a.kind() != b.kind()) return false;
    if (a.is(Kind::Function)) {
        const auto* fa = a.heap_as<FunctionObject>();
        const auto* fb = b.heap_as<FunctionObject>();
        return fa->code == fb->code && fa->name == fb->name && fa->native_id == fb->native_id &&
               fa->is_lambda == fb->is_lambda;
    }
    return equal(a, b);
}

// The construction the bulk pass replaces, kept here as the oracle.
Value reference_constants(const Program& source) {
    Value constants = make_array();
    for (const auto& proto : source.protos)
        for (const auto& v : proto->constants) array_push(constants, v);
    return constants;
}

std::size_t constant_total(const Program& program) {
    std::size_t n = 0;
    for (const auto& proto : program.protos) n += proto->constants.size();
    return n;
}

// Snapshots `program`, checks the result against the oracle, and returns the
// decoded constants (empty Value on failure).
void check_snapshot(const std::string& label, const Program& program) {
    ProgramTransferMap map(program);
    IsolateTaskInput input;
    std::shared_ptr<const Program> metadata;
    std::string error;
    {   // warm-up: first use initialises process-wide state (same with per-element construction)
        IsolateTaskInput warm; std::shared_ptr<const Program> warm_meta; std::string warm_error;
        (void)snapshot_isolate_program(program, map, warm, warm_meta, warm_error);
    }
    const std::size_t heap_before = live_heap_bytes();
    const bool ok = snapshot_isolate_program(program, map, input, metadata, error);
    CHECK(ok);
    if (!ok) { std::fprintf(stderr, "%s: snapshot failed: %s\n", label.c_str(), error.c_str()); return; }
    CHECK(live_heap_bytes() == heap_before);  // the local constants array is gone
    CHECK(input.has_program_snapshot);
    CHECK(metadata && metadata->protos.size() == program.protos.size());
    CHECK(input.constant_counts.size() == program.protos.size());
    CHECK(input.proto_wires.size() == program.protos.size());
    for (std::size_t i = 0; i < program.protos.size() && i < input.constant_counts.size(); ++i) {
        CHECK(input.constant_counts[i] == program.protos[i]->constants.size());
        std::uint32_t wire = ~std::uint32_t(0);
        (void)map.index_of(program.protos[i].get(), wire);
        CHECK(input.proto_wires[i] == wire);
        CHECK(metadata->protos[i]->constants.empty());  // metadata carries no heap-backed Value
    }

    TransferGraph oracle;
    {
        Value reference = reference_constants(program);
        CHECK(oracle.encode(reference, map, error) == TransferStatus::Ok);
    }
    CHECK(input.constants_graph.node_count() == oracle.node_count());
    CHECK(input.constants_graph.storage_bytes() == oracle.storage_bytes());

    Value got, want;
    CHECK(input.constants_graph.decode(map, got, error) == TransferStatus::Ok);
    CHECK(oracle.decode(map, want, error) == TransferStatus::Ok);
    CHECK(got.is(Kind::Array) && want.is(Kind::Array));
    if (!got.is(Kind::Array) || !want.is(Kind::Array)) return;
    const auto& g = array_items(got);
    const auto& w = array_items(want);
    CHECK(g.size() == constant_total(program));
    CHECK(g.size() == w.size());
    bool all_same = g.size() == w.size();
    for (std::size_t i = 0; all_same && i < g.size(); ++i) all_same = same_constant(g[i], w[i]);
    CHECK(all_same);
    // The flattened order is proto order, then each proto's constant order.
    std::size_t k = 0;
    bool in_order = true;
    for (const auto& proto : program.protos)
        for (const auto& v : proto->constants) {
            if (k >= g.size() || g[k].kind() != v.kind() ||
                (!v.is(Kind::Function) && !equal(g[k], v))) in_order = false;
            ++k;
        }
    CHECK(in_order);
    std::size_t functions = 0, strings = 0;
    for (const auto& v : g) { functions += v.is(Kind::Function); strings += v.is(Kind::String); }
    std::printf("snapshot-constants %s: protos=%zu constants=%zu (functions=%zu strings=%zu) nodes=%zu\n",
                label.c_str(), program.protos.size(), g.size(), functions, strings,
                input.constants_graph.node_count());
}

void compile_and_check(const std::string& label, const std::string& source) {
    CompileLimits limits; limits.parallel_tasks = true;
    auto code = rt_vm_tool::compile(source, limits);
    CHECK(code.program != nullptr);
    if (!code.program) { std::fprintf(stderr, "%s: refused: %s\n", label.c_str(), code.reason.c_str()); return; }
    check_snapshot(label, *code.program);
}

// A program shaped like the nested isolate clones of the recursive-await
// witness: many protos, each with a growing constant pool.
void deep_chain() {
    std::string source;
    for (int i = 0; i < 60; ++i) {
        source += "fn(g" + std::to_string(i) + "(x)){y:=x;";
        for (int j = 0; j <= i; ++j) source += "y=y+" + std::to_string(1000 + i * 100 + j) + ";";
        source += "return y};";
    }
    source += "print(g59(1))";
    compile_and_check("deep-chain", source);
}

}  // namespace

int main() {
    compile_and_check("literals",
        "a:=1;b:=-7;c:=2.5;d:=\"short\";e:=\"a string constant longer than fourteen bytes\";"
        "f:=true;print(a);print(b);print(c);print(d);print(e);print(f)");
    compile_and_check("functions",
        "fn(h(x)){return x+1};fn(s(t)){return t+\"tail\"};k:=h;"
        "print(h(4));print(s(\"x\"));print(k(2))");
    compile_and_check("recursive-await",
        "fn[async](f(n)){if(n==0){return 0};return (await f(n-1))+1};print(await f(100))");
    compile_and_check("no-constants", "x:=[]");
    deep_chain();
    return rt_test::finish("cp23-snapshot-constants-unit");
}
