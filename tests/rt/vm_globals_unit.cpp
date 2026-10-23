// Runtime 2.0 CP12 phase B: unit tests for globals below the compiler (H1).
// GetGlobal/SetGlobal executed by the VM on hand-built bytecode, the globals
// analysis (rt/globals.h) and the kind inference's shared slot (infer.h),
// FunctionBuilder::keep_type. The compiler does not emit the opcodes yet
// (its hookup waits for the CP15 integration, cp12b-resolver.md H1).
#include "rt/codegen.h"
#include "rt/globals.h"
#include "rt/infer.h"
#include "rt/parser.h"
#include "rt/resolver.h"
#include "rt/vm.h"
#include "rt_test.h"

#include <memory>
#include <string>
#include <string_view>

using namespace nift::rt;

namespace {

// main: g := 10 (r1); r2 := f(); return g + r2 * 100.
// f:    t := G[1] + 5; G[1] = t; return t.
std::unique_ptr<Program> global_program(std::uint16_t& main_regs) {
    auto program = std::make_unique<Program>();
    FunctionBuilder f("f");
    std::uint16_t r0 = 0, t = 0;
    f.alloc(r0);
    f.alloc(t);
    f.emit(OpCode::GetGlobal, t, 1);
    f.emit(OpCode::AddI, t, t, jump_operand(5));
    f.emit(OpCode::SetGlobal, 1, t);
    f.emit(OpCode::Return, t);
    FunctionBuilder m("");
    std::uint16_t m0 = 0, g = 0, call = 0, k100 = 0, prod = 0, sum = 0;
    m.alloc(m0);
    m.alloc(g);
    m.emit(OpCode::LoadI, g, jump_operand(10));
    m.alloc(call);
    m.emit(OpCode::Call, call, 1, 0);
    m.alloc(prod);
    m.constant(Value::integer(100), k100);
    m.emit(OpCode::Mul, prod, call, static_cast<std::uint16_t>(k100 | kConstantBit));
    m.alloc(sum);
    m.emit(OpCode::Add, sum, g, prod);
    m.emit(OpCode::TopReturn, sum);
    main_regs = m.top();
    program->protos.push_back(m.finish());
    program->protos.push_back(f.finish());
    return program;
}

void check_vm_opcodes() {
    std::uint16_t regs = 0;
    const auto program = global_program(regs);
    Vm vm(*program);
    std::string output;
    const VmResult r = vm.run(output);
    CHECK(r.reason == StopReason::None);
    CHECK(r.returned);
    CHECK(r.return_text == "1515");  // g = 15 after f; f returned 15
    if (r.return_text != "1515") std::fprintf(stderr, "  got [%s] %s\n", r.return_text.c_str(), r.message.c_str());
    CHECK(disassemble(*program).find("GetGlobal") != std::string::npos);
}

struct Unit {
    std::unique_ptr<Ast> ast;
    NodeId root = kNoNode;
    UnitPlan plan;
    GlobalPlan globals;
};

Unit analyse(const std::string& source, std::string& keep) {
    keep = source;
    Unit u;
    u.ast = std::make_unique<Ast>(keep);
    const ParseResult parsed = parse_script(*u.ast);
    u.root = parsed.root;
    u.plan = plan_unit(*u.ast, u.root);
    u.globals = plan_globals(*u.ast, u.root, u.plan);
    return u;
}

// Kinds of every Ident `name` inside function `f` (or main when f < 0), OR-ed.
KindSet ident_kinds(const Unit& u, const UnitKinds& kinds, std::string_view name, int f) {
    const Node& fn = f >= 0 ? u.ast->at(u.plan.functions[static_cast<std::size_t>(f)]) : u.ast->at(u.root);
    KindSet out = 0;
    for (NodeId id = 0; id < u.ast->size(); ++id) {
        const Node& n = u.ast->at(id);
        if (n.kind != NodeKind::Ident || u.ast->text(n) != name || n.begin < fn.begin || n.end > fn.end) continue;
        const std::string_view rest = u.ast->source().substr(n.end);
        const std::size_t next = rest.find_first_not_of(" \t");
        if (next != std::string_view::npos && rest.substr(next, 2) == ":=") continue;  // a declaration's name
        if (f < 0) {  // main: outside every function
            bool inside = false;
            for (NodeId g : u.plan.functions)
                inside = inside || (n.begin >= u.ast->at(g).begin && n.end <= u.ast->at(g).end);
            if (inside) continue;
        }
        out |= kinds.of(id);
    }
    return out;
}

void check_analysis() {
    std::string keep;
    {
        const Unit u = analyse("g := \"s\"\nfn(f()) { return g }\nprint(f())\n", keep);
        CHECK(u.plan.ok);
        CHECK(u.globals.free.size() == 1 && u.globals.free[0].count("g") && u.globals.free[0].at("g").ok);
        const UnitKinds without = infer_unit_kinds(*u.ast, u.root, u.plan);
        const UnitKinds with = infer_unit_kinds(*u.ast, u.root, u.plan, 0, &u.globals);
        CHECK(with.converged && without.converged);
        CHECK(ident_kinds(u, without, "g", 0) & kKindUnknown);
        CHECK(ident_kinds(u, with, "g", 0) == kKindString);
    }
    {
        // A function's write widens the global for main too (one shared slot).
        const Unit u = analyse("g := 1\nfn(f()) { g = \"s\"\nreturn 0 }\nprint(f())\nprint(g)\n", keep);
        CHECK(u.globals.free[0].at("g").ok && u.globals.free[0].at("g").written);
        const UnitKinds with = infer_unit_kinds(*u.ast, u.root, u.plan, 0, &u.globals);
        CHECK(with.converged);
        CHECK((ident_kinds(u, with, "g", -1) & (kKindInt | kKindString)) == (kKindInt | kKindString));
        CHECK(!(ident_kinds(u, with, "g", -1) & kKindUnknown));
    }
    {
        // G1-G3 refusals.
        const Unit dyn = analyse("fn(inner()) { return z }\nfn(outer()) { z := 1\nreturn inner() }\nprint(outer())\n",
                                 keep);
        CHECK(dyn.globals.free[0].at("z").refusal == "free-name:dynamic-caller");
        const Unit shadow = analyse("z := 5\nfn(inner()) { return z }\nfn(outer()) { z := 1\nreturn inner() }\n"
                                    "print(outer())\n", keep);
        CHECK(shadow.globals.free[0].at("z").refusal == "free-name:dynamic-shadow");
        const Unit early = analyse("fn(f()) { return g }\nprint(f())\ng := 1\n", keep);
        CHECK(early.globals.free[0].at("g").refusal == "free-name:before-declaration");
        const Unit self = analyse("fn(f()) { return 1 }\ng := f() + 1\nfn(h()) { return g }\ng2 := h()\n", keep);
        CHECK(self.globals.free[1].at("g").ok);
        const Unit decl_calls = analyse("fn(h()) { return g }\ng := h()\n", keep);
        CHECK(decl_calls.globals.free[0].at("g").refusal == "free-name:before-declaration");
        const Unit block = analyse("g := 1\nif (true) { g := 2 }\nfn(f()) { return g }\nprint(f())\n", keep);
        CHECK(block.globals.free[0].at("g").refusal == "free-name:not-one-top-level-binding");
        const Unit legacy = analyse("fn(f()) { return args }\nprint(f())\n", keep);
        CHECK(legacy.globals.free[0].at("args").refusal == "free-name:legacy-binding");
        const Unit none = analyse("fn(f()) { return q }\nprint(f())\n", keep);
        CHECK(none.globals.free[0].at("q").refusal == "free-name:undeclared");
        const Unit local = analyse("fn(f(q)) { r := q\nreturn r }\nprint(f(1))\n", keep);
        CHECK(local.globals.free[0].empty());
    }
}

void check_keep_type() {
    FunctionBuilder b("k");
    std::uint16_t v = 0, t = 0, u = 0, w = 0, minus = 0;
    b.alloc(t);
    b.alloc(v);
    b.alloc(u);
    b.alloc(w);
    b.constant(Value::integer(-1), minus);
    b.emit(OpCode::BindType, t, v, static_cast<std::uint16_t>(minus | kConstantBit));
    b.emit(OpCode::BindType, u, w, static_cast<std::uint16_t>(minus | kConstantBit));
    b.keep_type(t);
    b.emit(OpCode::Halt);
    b.drop_unread_types();
    const auto proto = b.finish();
    std::size_t binds = 0;
    for (const Instr& in : proto->code) binds += in.op == OpCode::BindType;
    CHECK(binds == 1);  // t kept (read by functions), u dropped
}

} // namespace

int main() {
    check_vm_opcodes();
    check_analysis();
    check_keep_type();
    return rt_test::finish("rt vm globals unit");
}
