#include "rt/compiler_session.h"
#include "rt/compiler.h"
#include "rt/session_shape.h"
#include "rt/legacy_names.h"
#include <algorithm>

// rt-lint: cold-file incremental compiler namespace publication
namespace nift::rt::detail {
void UnitCompiler::set_session(SessionCompileContext* session) {
    session_ = session;
    if (!session_) return;
    for (const auto& symbol : session_->symbols) {
        Binding binding;
        binding.global = binding.cell = binding.typed = true;
        binding.session_name = symbol.name;
        const auto declaration = session_->declarations.find(symbol.name);
        if (declaration != session_->declarations.end()) binding.decl_node = declaration->second;
        binding.reg = symbol.slot;
        binding.type_reg = symbol.slot + 1;
        binding.decl_kinds = symbol.kinds;
        binding.const_array = symbol.const_array; binding.readonly = symbol.readonly;
        session_bindings_.emplace(symbol.name, binding);
    }
}
bool UnitCompiler::session_render(NodeId id) {
    const auto& node = ast_.at(id);
    const auto expression = node.a;
    std::uint16_t value = 0;
    if (!temp(value) || !expr_to(expression, value)) return false;
    const auto source = std::string(text(node));
    const auto last = source.find_last_not_of(" \t\r\n");
    fb_->set_offset(node.begin);
    fb_->note(fb_->emit(OpCode::SessionRender, value,
        last != std::string::npos && source[last] == ')' ? 1 : 0), source);
    return true;
}
bool UnitCompiler::session_admit() {
    // Other retained readers share these cells and keep their original
    // inferred bounds, even when this command specializes a new caller.
    for (const auto& binding : kinds_.bindings) {
        const auto bound = session_->capture_contracts.find(binding.second);
        if (bound == session_->capture_contracts.end()) continue;
        const auto shape = kinds_.session_shapes.find(binding.first);
        if (shape != kinds_.session_shapes.end() && !session_shape_within(shape->second, bound->second))
            return fail(ast_.at(binding.first), "session:capture-contract");
    }
    // Legacy named functions can see caller-local bindings. Only names read
    // freely by a named function need the D-038 shadow guard; ordinary lexical
    // shadowing that cannot influence such a reader remains admitted.
    std::unordered_set<std::string_view> dynamic_reads;
    if (globals_) for (const auto& function : globals_->free)
        for (const auto& ref : function)
            for (const auto& symbol : session_->symbols)
                if (symbol.name == ref.first) dynamic_reads.insert(ref.first);
    std::vector<NodeId> pending{root_};
    while (!pending.empty()) {
        const auto id = pending.back(); pending.pop_back();
        if (id == kNoNode) continue;
        const auto& n = ast_.at(id);
        if (n.kind == NodeKind::StructDecl) return fail(n, "session:struct-schema");
        if ((n.kind == NodeKind::Assign || n.kind == NodeKind::Declare) &&
            n.a != kNoNode && ast_.at(n.a).kind == NodeKind::Ident) {
            const auto name = text(ast_.at(n.a));
            const auto bound = kinds_.bindings.find(n.a);
            for (const auto& symbol : session_->symbols) if (symbol.name == name) {
                const auto external = session_->declarations.find(symbol.name);
                if (n.kind == NodeKind::Assign && bound != kinds_.bindings.end() &&
                    external != session_->declarations.end() && bound->second != external->second) continue;
                if (n.kind == NodeKind::Declare) {
                    bool top = false;
                    for (std::uint32_t k = 0; k < ast_.at(root_).count; ++k)
                        top = top || ast_.list(ast_.at(root_))[k] == id;
                    if (!top) continue;
                }
                if (n.kind == NodeKind::Declare && symbol.defined)
                    return fail(n, "session:shadow-or-redeclare");
                if (symbol.callable && n.kind != NodeKind::Declare) return fail(n, "session:callable-rebind");
                if (kinds(n.b) & ~symbol.contract) return fail(n, "session:binding-contract");
                const auto shape = kinds_.session_shapes.find(n.b);
                if (shape != kinds_.session_shapes.end() &&
                    !session_shape_within(shape->second, symbol.structure))
                    return fail(n, "session:binding-shape-contract");
            }
        }
        NodeId receiver = kNoNode, value = kNoNode;
        if (n.kind == NodeKind::Assign && ast_.at(n.a).kind == NodeKind::Index) {
            receiver = ast_.at(n.a).a; value = n.b;
        } else if (n.kind == NodeKind::Call && ast_.at(n.a).kind == NodeKind::Member) {
            const auto& member = ast_.at(n.a);
            const auto method = text(ast_.at(member.b));
            if (method == "push" && n.count == 1) { receiver = member.a; value = ast_.list(n)[0]; }
            if (method == "insert" && n.count == 2) { receiver = member.a; value = ast_.list(n)[1]; }
            if (method == "splice" && n.count == 3) { receiver = member.a; value = ast_.list(n)[2]; }
        }
        if (receiver != kNoNode && ast_.at(receiver).kind == NodeKind::Ident) {
            for (const auto& symbol : session_->symbols)
                if (symbol.name == text(ast_.at(receiver)) && symbol.structure) {
                const auto shape = kinds_.session_shapes.find(value);
                if (shape != kinds_.session_shapes.end()) {
                    auto added = shape->second;
                    if (n.kind == NodeKind::Call && text(ast_.at(ast_.at(n.a).b)) == "splice") added = added->elem;
                    if (!session_shape_within(added, symbol.structure->elem))
                        return fail(n, "session:element-contract");
                }
            }
        }
        const Node* declared = n.kind == NodeKind::Param ? &n :
            n.kind == NodeKind::Declare && n.a != kNoNode && ast_.at(n.a).kind == NodeKind::Ident &&
            !top_level_decl_ids_.count(id) ? &ast_.at(n.a) : nullptr;
        if (declared && dynamic_reads.count(text(*declared)))
            return fail(n, "session:dynamic-shadow");
        for (auto child : {n.a, n.b, n.c}) pending.push_back(child);
        for (std::uint32_t k = 0; k < n.count; ++k) pending.push_back(ast_.list(n)[k]);
    }
    return true;
}
bool UnitCompiler::session_call_types(const Node& node, NodeId callee, std::uint16_t base) {
    if (ast_.at(callee).kind == NodeKind::Member &&
        legacy_special_method(text(ast_.at(ast_.at(callee).b))))
        return fail(node, "session:legacy-method-precedence");
    const auto name = ast_.at(callee).kind == NodeKind::Ident ? text(ast_.at(callee)) : std::string_view{};
    for (const auto& symbol : session_->symbols) {
        if (symbol.name != name || !symbol.callable || session_->lambda_templates.count(symbol.name)) continue;
        if (symbol.definition) {
            const auto target = plan_.index.find(symbol.definition->name);
            if (target != plan_.index.end()) {
                const auto f = target->second - 1u;
                if (node.count != ast_.at(plan_.functions[f]).count) return true;
                for (std::uint32_t k = 0; k < node.count; ++k) {
                    const auto& argument = ast_.at(ast_.list(node)[k]);
                    if (argument.kind == NodeKind::Member || argument.kind == NodeKind::Index)
                        return fail(argument, "session:call-alias-contract");
                }
                return pass_param_types(f, node.count, ast_.list(node), base);
            }
        }
        // An arity mismatch is an existing VM runtime error, after arguments.
        if (node.count != symbol.arity) return true;
        if (symbol.params.size() != node.count) return fail(node, "session:call-signature");
        for (std::uint32_t k = 0; k < node.count; ++k) {
            const auto argument = ast_.list(node)[k];
            if (!symbol.params[k] || (kinds(argument) & ~symbol.params[k]))
                return fail(ast_.at(argument), "session:call-kind-contract");
            // Existing parameter alias admission cannot be weakened across a
            // unit boundary. Places stay refused until their escape summary
            // is carried along with the compiled callable's signature.
            if (ast_.at(argument).kind == NodeKind::Index || ast_.at(argument).kind == NodeKind::Member)
                return fail(ast_.at(argument), "session:call-alias-contract");
        }
        for (std::uint32_t k = 0; k < symbol.param_types.size(); ++k) {
            if (symbol.param_types[k] == 0xffff) continue;
            const auto argument = ast_.list(node)[k];
            if (loop_reachable() && type_mismatch_possible(text_type_at(argument), kinds(argument), scope_index()))
                return fail(ast_.at(argument), "context:parameter-type");
            const auto target = static_cast<std::uint16_t>(base + symbol.param_types[k]);
            while (fb_->top() <= target) { std::uint16_t reg = 0; if (!temp(reg)) return false; }
            const auto mark = fb_->top();
            std::uint16_t operand = 0;
            if (!text_type_of(text_type_at(argument), ast_.at(argument), operand)) return false;
            if (operand & kConstantBit) fb_->emit(OpCode::LoadK, target, operand & ~kConstantBit);
            else if (operand != target) fb_->emit(OpCode::Move, target, operand);
            fb_->release_to(mark);
        }
        return true;
    }
    // Cross-unit structural shapes intentionally carry no old AST callable
    // targets. Never execute an uninstantiated parameter body through an
    // object field or other dynamic flow merely because it has Function kind.
    const auto targets = kinds_.callable_targets.find(callee);
    if (node.count && (targets == kinds_.callable_targets.end() || targets->second.empty()))
        return fail(node, "session:callable-flow");
    if (targets != kinds_.callable_targets.end()) for (const auto target : targets->second) {
        if (ast_.at(target).kind != NodeKind::FnDecl) continue;
        const auto found = std::find(plan_.functions.begin(), plan_.functions.end(), target);
        if (found == plan_.functions.end() || targets->second.size() != 1)
            return fail(node, "session:callable-flow");
        const auto f = static_cast<std::size_t>(found - plan_.functions.begin());
        if (node.count == ast_.at(target).count)
            return pass_param_types(f, node.count, ast_.list(node), base);
    }
    return true;
}
bool UnitCompiler::session_publish(std::string_view name, const Binding& binding) {
    const auto existing = std::find_if(session_->symbols.begin(), session_->symbols.end(),
        [&](const auto& symbol) { return symbol.name == name; });
    if (existing != session_->symbols.end() && existing->defined &&
        !(binding.readonly && binding.decl_kinds == kKindFunction && existing->readonly && existing->callable))
        return fail(ast_.at(root_), "session:redeclared");
    if (existing != session_->symbols.end() && !existing->defined &&
        (binding.readonly != existing->readonly || binding.const_array != existing->const_array))
        return fail(ast_.at(root_), "session:reserved-binding-contract");
    if (session_->slots > 65532) return fail(ast_.at(root_), "limit:session-bindings");
    Binding global = binding;
    global.global = global.cell = true;
    global.session_name = std::string(name);
    global.reg = existing == session_->symbols.end() ? session_->slots : existing->slot;
    global.type_reg = global.reg + 1;
    if (existing == session_->symbols.end()) session_->slots += 2;
    fb_->emit(OpCode::SetGlobal, global.reg, binding.reg);
    fb_->emit(OpCode::SetGlobal, global.type_reg, binding.type_reg);
    fb_->emit(OpCode::MarkGlobal, global.reg);
    session_bindings_[std::string(name)] = global;
    top_level_bindings_[name] = global;
    global_bindings_[name] = global;
    scopes_.update(name, global);
    SessionSymbol symbol;
    symbol.name = std::string(name);
    symbol.slot = global.reg;
    symbol.kinds = symbol.contract = binding.decl_kinds;
    const auto shape = kinds_.binding_shapes.find(std::string(name));
    if (shape != kinds_.binding_shapes.end()) {
        symbol.structure = session_shape(shape->second);
        symbol.kinds = symbol.contract = symbol.structure->kinds;
    }
    symbol.const_array = binding.const_array; symbol.readonly = binding.readonly;
    if (existing == session_->symbols.end()) session_->symbols.push_back(std::move(symbol));
    else {
        symbol.contract = existing->contract;
        // Older closures were compiled against the reserved slot's complete
        // bounds, including mutations after its original declaration.
        if (!existing->defined) symbol.structure = existing->structure;
        *existing = std::move(symbol);
    }
    return true;
}
bool UnitCompiler::session_function(NodeId id) {
    const Node& node = ast_.at(id);
    const auto name = text(ast_.at(node.a));
    if (session_->imported_functions.count(std::string(name))) return true;
    for (const auto& symbol : session_->symbols) if (symbol.name == name && symbol.defined) {
        for (const auto& retained : session_->symbols)
            if (retained.untracked_callables) return fail(node, "session:redefinition-callable-flow");
        bool prior_statement = false;
        for (std::uint32_t k = 0; k < ast_.at(root_).count; ++k) {
            const auto statement = ast_.list(ast_.at(root_))[k];
            if (statement == id) break;
            prior_statement = prior_statement || ast_.at(statement).kind != NodeKind::FnDecl;
        }
        if (prior_statement) return fail(node, "session:redefinition-order");
    }
    Binding binding;
    std::uint16_t value = 0;
    if (!temp(value) || !temp(binding.type_reg) || !temp(binding.reg)) return false;
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::LoadFunction, value, plan_.index.at(name));
    fb_->emit(OpCode::LoadI, binding.type_reg, 4);
    fb_->emit(OpCode::NewCell, binding.reg, value, binding.type_reg);
    binding.cell = binding.typed = binding.readonly = true;
    binding.decl_kinds = kKindFunction;
    if (!session_publish(name, binding)) return false;
    return true;
}
bool UnitCompiler::session_lambda_captures(NodeId id, const Node& node, std::vector<std::string_view>& names,
        std::vector<bool>& loop, std::vector<bool>& readonly, std::vector<bool>& const_array) {
    const auto imported = session_->template_definitions.find(id);
    if (imported != session_->template_definitions.end()) {
        const auto& captures = imported->second->captures;
        if (captures.size() != names.size()) return fail(node, "session:capture-layout");
        for (std::size_t k = 0; k < captures.size(); ++k) {
            if (captures[k].name != names[k]) return fail(node, "session:capture-layout");
            loop[k] = captures[k].loop; readonly[k] = captures[k].readonly;
            const_array[k] = captures[k].const_array;
        }
    }
    return true;
}
bool UnitCompiler::session_lambda_definition(const LambdaTask& task, Proto& out) {
    const auto imported = session_->template_definitions.find(task.lambda);
    if (imported != session_->template_definitions.end()) {
        const auto& expected = imported->second->captures;
        if (expected.size() != task.upvalue_names.size()) return fail(ast_.at(task.lambda), "session:capture-layout");
        for (std::size_t k = 0; k < expected.size(); ++k)
            if (expected[k].name != task.upvalue_names[k] || expected[k].loop != task.upvalue_loop[k] ||
                expected[k].readonly != task.upvalue_readonly[k] ||
                expected[k].const_array != task.upvalue_const_array[k])
                return fail(ast_.at(task.lambda), "session:capture-layout");
        out.session_definition = imported->second;
    } else {
        auto definition = std::make_shared<SessionDefinition>();
        definition->lambda = true; definition->source = session_->source_owner;
        definition->begin = ast_.at(task.lambda).begin; definition->end = ast_.at(task.lambda).end;
        for (std::size_t k = 0; k < task.upvalue_names.size(); ++k) {
            infer_detail::TyRef shape;
            for (const auto& binding : kinds_.bindings) if (binding.second == task.upvalue_decls[k]) {
                const auto found = kinds_.session_shapes.find(binding.first);
                if (found != kinds_.session_shapes.end())
                    shape = infer_detail::unite(shape ? shape : infer_detail::empty_ty(), found->second);
            }
            if (!shape) shape = infer_detail::kinds_ty(kinds(task.upvalue_decls[k]));
            definition->captures.push_back({std::string(task.upvalue_names[k]), session_shape(shape),
                task.upvalue_loop[k], task.upvalue_readonly[k], task.upvalue_const_array[k]});
        }
        out.session_definition = std::move(definition);
    }
    return true;
}
} // namespace nift::rt::detail
