#include "rt/compiler_process.h"
#include "rt/compiler_expr.h"
#include "rt/literal.h"
#include "rt/infer_types.h"
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

// Admit only a read of the stable local used as the stdout target of a
// supported JSON backend in this method. The success guard must dominate it.
bool UnitCompiler::package_json_source(const Node& node) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines || !plan_.is_method(function_) ||
        !package_process_scope(plan_, node) || node.count != 1 || plan_.index.count("cmd")) return false;
    const auto& path = ast_.at(ast_.list(node)[0]);
    if (path.kind != NodeKind::Ident) return false;
    const auto name = text(path);
    const auto* target = find_binding(name);
    if (!target || target->cell || target->global || target->upvalue || assigned_.count(name)) return false;
    const auto* cmd = find_binding("cmd");
    if (cmd && !cmd->invocation && !invocation_shadows_.count(cmd->decl_node)) return false;

    const auto& method = ast_.at(ast_.at(plan_.functions[function_]).b);
    const auto* items = ast_.list(method);
    const auto target_read = [&](NodeId id) {
        const auto& open = ast_.at(id);
        return open.kind == NodeKind::Call && open.count == 1 &&
               ast_.at(open.a).kind == NodeKind::Ident && text(ast_.at(open.a)) == "open" &&
               !find_binding("open") && !plan_.index.count("open") && !plan_.struct_index.count("open") &&
               ast_.at(ast_.list(open)[0]).kind == NodeKind::Ident &&
               text(ast_.at(ast_.list(open)[0])) == name;
    };
    const auto zero_argument_method = [&](NodeId id, std::string_view method_name) -> NodeId {
        const auto& invocation = ast_.at(id);
        if (invocation.kind != NodeKind::Call || invocation.count) return kNoNode;
        const auto& member = ast_.at(invocation.a);
        return member.kind == NodeKind::Member && text(ast_.at(member.b)) == method_name ? member.a : kNoNode;
    };
    const auto reads_trimmed = [&](NodeId id) {
        const NodeId source = zero_argument_method(id, "trim");
        return source != kNoNode && target_read(source);
    };

    bool declared = false, succeeded = false;
    const Node* process_result = nullptr;
    const Node* text_result = nullptr;
    for (std::uint32_t i = 0; i < method.count; ++i) {
        const auto& statement = ast_.at(items[i]);
        if (statement.kind == NodeKind::Declare && statement.b == node_id(node))
            return succeeded;
        if (statement.kind == NodeKind::Declare && statement.a == target->decl_node) {
            declared = true;
            continue;
        }
        if (!declared) continue;
        if (statement.kind == NodeKind::Command) return false;
        if (!process_result) {
            if (statement.kind != NodeKind::Declare || ast_.at(statement.b).kind != NodeKind::Call) continue;
            NodeId output = kNoNode;
            const auto* command = process_pipeline_command(ast_, ast_.at(statement.b), output);
            if (!command || ast_.at(output).kind != NodeKind::Ident || text(ast_.at(output)) != name) continue;
            if (command->count < 2 || !process_program_literal(ast_, *command)) continue;
            const auto* args = ast_.list(*command);
            if ((text(ast_.at(args[0])) == R"("sqlite3")" && text(ast_.at(args[1])) == R"("-json")") ||
                (text(ast_.at(args[0])) == R"("redis-cli")" && text(ast_.at(args[1])) == R"("--json")"))
                process_result = &ast_.at(statement.a);
            continue;
        }
        if (succeeded && !text_result && statement.kind == NodeKind::Declare && reads_trimmed(statement.b)) {
            text_result = &ast_.at(statement.a);
            continue;
        }
        if (statement.kind != NodeKind::If || statement.c != kNoNode ||
            infer_detail::block_falls_through(ast_, statement.b)) return false;
        if (!succeeded) {
            const auto& check = ast_.at(statement.a);
            if (check.kind != NodeKind::Binary || check.op != Op::Ne || text(ast_.at(check.b)) != "0") return false;
            const auto& status = ast_.at(check.a);
            if (status.kind != NodeKind::Member || ast_.at(status.a).kind != NodeKind::Ident ||
                text(ast_.at(status.a)) != text(*process_result) || text(ast_.at(status.b)) != "exit_code" ||
                assigned_.count(text(*process_result))) return false;
            succeeded = true;
            continue;
        }
        const NodeId empty_source = zero_argument_method(statement.a, "empty");
        if (empty_source == kNoNode ||
            !(reads_trimmed(empty_source) ||
              (text_result && ast_.at(empty_source).kind == NodeKind::Ident &&
               text(ast_.at(empty_source)) == text(*text_result)))) return false;
    }
    return false;
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
