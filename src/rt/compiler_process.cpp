#include "rt/compiler_process.h"
#include "rt/compiler_expr.h"
#include "rt/literal.h"
#include "rt/module_link.h"
#include "rt/native.h"
#include "rt/string.h"

// rt-lint: cold-file process call shape and literal validation
namespace nift::rt::detail {

bool package_process_scope(const UnitPlan& plan, const Node& call) {
    // D-065 package amendment: imported-module code. Entry scripts keep
    // their original refusal contracts, including the read-limit witnesses.
    return plan.linked && plan.linked->locate(call.begin).module > 0;
}

bool process_program_literal(const Ast& ast, const Node& call) {
    if (!call.count) return false;
    const Node& program = ast.at(ast.list(call)[0]);
    Value decoded;
    if (program.kind != NodeKind::String || program.count ||
        !decode_string_literal(ast.text(program), decoded) || !decoded.is(Kind::String))
        return false;
    const auto executable = string_view_of(decoded);
    return !executable.empty() &&
           executable.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") ==
               std::string_view::npos;
}

const Node* process_pipeline_command(const Ast& ast, const Node& node, NodeId& output) {
    const Node& method = ast.at(node.a);
    if (method.kind != NodeKind::Member || node.count || ast.text(ast.at(method.b)) != "run") return nullptr;
    const Node& redirect = ast.at(method.a);
    if (redirect.kind != NodeKind::Call || redirect.count != 1) return nullptr;
    const Node& redirect_method = ast.at(redirect.a);
    if (redirect_method.kind != NodeKind::Member || ast.text(ast.at(redirect_method.b)) != "stdout") return nullptr;
    const Node& command = ast.at(redirect_method.a);
    if (command.kind != NodeKind::Call) return nullptr;
    const Node& callee = ast.at(command.a);
    if (callee.kind != NodeKind::Ident || ast.text(callee) != "cmd") return nullptr;
    output = ast.list(redirect)[0];
    return &command;
}

int UnitCompiler::process_call(const Node& node, const Node& command, NodeId output, std::uint16_t dst) {
    if (session_ || !package_process_scope(plan_, node) || !process_program_literal(ast_, command)) return -1;
    const auto* args = ast_.list(command);
    const std::uint32_t count = command.count + (output != kNoNode);
    const auto base = fb_->top();
    if (count > kCallWindowMask) return fail(node, "limit:arguments");
    if (!operand_window(base, count)) return 0;
    using natives::BuiltinId;
    const auto native = [&](BuiltinId id, std::uint16_t slot, std::uint16_t argc, const Node& origin) {
        fb_->set_offset(site(origin));
        fb_->emit(OpCode::CallNative, slot, kFirstBuiltin + static_cast<std::uint16_t>(id), argc);
    };
    if (output == kNoNode) native(BuiltinId::ProcessCheck, base, 0, node);
    if (!window_value(args[0], base)) return 0;
    for (std::uint32_t k = 1; k < command.count; ++k) {
        const Node& arg = ast_.at(args[k]);
        const bool spread = arg.kind == NodeKind::Spread;
        const auto slot = static_cast<std::uint16_t>(base + k);
        if (!window_value(spread ? arg.a : args[k], slot)) return 0;
        // The extra slot is still vacant. It selects the cmd() diagnostic;
        // the next operand overwrites it after validation.
        if (output != kNoNode) fb_->emit(OpCode::LoadNull, static_cast<std::uint16_t>(slot + 1));
        native(spread ? BuiltinId::ProcessSpread : BuiltinId::ProcessArg, slot,
               output == kNoNode ? 1 : 2, arg);
    }
    if (output != kNoNode && !window_value(output, static_cast<std::uint16_t>(base + command.count)))
        return 0;
    native(output == kNoNode ? BuiltinId::ProcessRun : BuiltinId::ProcessRunStdout,
           base, static_cast<std::uint16_t>(count), node);
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(base);
    return builder_ok();
}

int UnitCompiler::process_pipeline(const Node& node, std::uint16_t dst) {
    NodeId output;
    const Node* command = process_pipeline_command(ast_, node, output);
    if (!command || plan_.index.count("cmd")) return -1;
    const Node& redirect = ast_.at(ast_.at(node.a).a);
    const Node& redirect_method = ast_.at(redirect.a);
    const Binding* binding = find_binding("cmd");
    if (binding && !binding->invocation && !invocation_shadows_.count(binding->decl_node)) return -1;
    if (!plain_postfix(redirect) || !plain_postfix(redirect_method) || !plain_postfix(*command) ||
        !no_trailing_comma(redirect) || !no_trailing_comma(*command)) return 0;
    return process_call(node, *command, output, dst);
}
} // namespace nift::rt::detail
