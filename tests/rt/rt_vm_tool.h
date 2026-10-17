#pragma once
// In-process helpers for the Runtime 2.0 VM tests and tools (CP11): parse,
// compile and run a script without the nift CLI.
#include "rt/compiler.h"
#include "rt/parser.h"
#include "rt/vm.h"

#include <memory>
#include <string>

namespace rt_vm_tool {

struct Compiled {
    std::string source;
    std::unique_ptr<nift::rt::Ast> ast;
    std::unique_ptr<nift::rt::Program> program;
    std::string reason;  // empty when compiled
};

// Opaque nodes and parse errors are reported as reasons, like the runner
// (`opaque:<class>` of the first Opaque node in pre-order).
inline std::string first_opaque(const nift::rt::Ast& ast, nift::rt::NodeId id) {
    if (id == nift::rt::kNoNode) return {};
    const nift::rt::Node& n = ast.at(id);
    if (n.kind == nift::rt::NodeKind::Opaque) return std::string("opaque:") + nift::rt::opaque_reason_name(n.reason);
    for (nift::rt::NodeId child : {n.a, n.b, n.c}) {
        std::string r = first_opaque(ast, child);
        if (!r.empty()) return r;
    }
    const nift::rt::NodeId* list = ast.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k) {
        std::string r = first_opaque(ast, list[k]);
        if (!r.empty()) return r;
    }
    return {};
}

inline Compiled compile(const std::string& source) {
    Compiled c;
    c.source = source;
    c.ast = std::make_unique<nift::rt::Ast>(c.source);
    const nift::rt::ParseResult parsed = nift::rt::parse_script(*c.ast);
    if (!parsed.ok) { c.reason = "parse-error"; return c; }
    c.reason = first_opaque(*c.ast, parsed.root);
    if (!c.reason.empty()) return c;
    nift::rt::CompileResult result = nift::rt::compile_unit(*c.ast, parsed.root);
    if (!result.program) { c.reason = result.reason; return c; }
    c.program = std::move(result.program);
    return c;
}

struct Run {
    std::string reason;  // compile fallback reason, empty if compiled
    nift::rt::VmResult result;
    std::string output;
};

inline Run run(const std::string& source, nift::rt::VmOptions options = nift::rt::VmOptions()) {
    Run r;
    Compiled c = compile(source);
    r.reason = c.reason;
    if (!c.program) return r;
    nift::rt::Vm vm(*c.program, options);
    r.result = vm.run(r.output);
    if (r.result.returned && !r.result.return_text.empty()) r.output += r.result.return_text + "\n";
    return r;
}

} // namespace rt_vm_tool
