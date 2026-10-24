#include "rt/compiler_unit.h"

#include <unordered_set>

// Unit-level analyses of the compiler (CP12): the parameters whose textual
// (P5) types every call passes, and the bindings whose binding type may be
// double (for the D-018 context refusals); then one proto per function.

namespace nift::rt::detail {

namespace {

void binding_leaves(const TextType& type, std::unordered_set<std::string_view>& out) {
    if (type.kind == TextType::Kind::Binding) out.insert(type.binding);
    if (type.kind == TextType::Kind::Meet) {
        binding_leaves(*type.left, out);
        binding_leaves(*type.right, out);
    }
}

struct ArgUse {
    std::size_t callee;
    std::uint32_t index;
    std::unordered_set<std::string_view> names;  // bindings named by the argument's text
};

// Names whose binding type legacy reads in `body` whatever the callees do:
// assignment targets and the bindings named by declaration and assignment
// texts; and, per user-call argument, the bindings its text names (read only
// when the callee's parameter is observable).
void typed_names(const Ast& ast, const UnitPlan& plan, NodeId body,
                 const std::unordered_set<std::string_view>& assigned, std::unordered_set<std::string_view>& out,
                 std::vector<ArgUse>& args) {
    out.insert(assigned.begin(), assigned.end());
    std::vector<NodeId> stack{body};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast.at(id);
        if ((n.kind == NodeKind::Declare || n.kind == NodeKind::Assign) && n.b != kNoNode)
            binding_leaves(infer_text_type(ast.text(ast.at(n.b))), out);
        const NodeId* list = ast.list(n);
        if (n.kind == NodeKind::Call && ast.at(n.a).kind == NodeKind::Ident) {
            const auto fn = plan.index.find(ast.text(ast.at(n.a)));
            if (fn != plan.index.end())
                for (std::uint32_t k = 0; k < n.count; ++k) {
                    ArgUse use{fn->second - 1u, k, {}};
                    binding_leaves(infer_text_type(ast.text(ast.at(list[k]))), use.names);
                    if (!use.names.empty()) args.push_back(std::move(use));
                }
        }
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
    }
}

} // namespace

UnitCompiler::UnitCompiler(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds,
                           const std::vector<bool>& loop_called, const GlobalPlan* globals)
    : globals_(globals), ast_(ast), plan_(plan), kinds_(kinds), root_(root), loop_called_(loop_called) {
    const std::size_t count = plan.functions.size();
    observable_.resize(count);
    std::vector<std::unordered_set<std::string_view>> names(count);
    std::vector<std::vector<ArgUse>> args(count);
    assigned_fn_.resize(count);
    for (std::size_t f = 0; f < count; ++f) {
        collect_assigned(ast, ast.at(plan.functions[f]).b, assigned_fn_[f]);
        typed_names(ast, plan, ast.at(plan.functions[f]).b, assigned_fn_[f], names[f], args[f]);
        observable_[f].assign(ast.at(plan.functions[f]).count, false);
    }
    // Fixpoint: an argument's named bindings are read when the callee's
    // parameter is observable (its type then travels to the callee).
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t f = 0; f < count; ++f) {
            for (const ArgUse& use : args[f])
                if (observable_[use.callee][use.index]) names[f].insert(use.names.begin(), use.names.end());
            const Node& fn = ast.at(plan.functions[f]);
            const NodeId* params = ast.list(fn);
            for (std::uint32_t k = 0; k < fn.count; ++k)
                if (!observable_[f][k] && names[f].count(ast.text(ast.at(params[k])))) {
                    observable_[f][k] = true;
                    changed = true;
                }
        }
    }
    compute_may_double();
}

const TextType& UnitCompiler::text_type_at(NodeId id) const {
    if (text_types_.empty()) text_types_.resize(ast_.size());
    std::shared_ptr<const TextType>& slot = text_types_[id];
    if (!slot) slot = std::make_shared<const TextType>(infer_text_type(type_text(ast_.at(id))));
    return *slot;
}

bool UnitCompiler::binding_error_possible(const Binding& b, KindSet value) const {
    if ((b.decl_kinds | value) & kKindUnknown) return true;
    // Binding types (tags) the declaration can give, and value tags: an int
    // value is accepted by a double binding; any other pair must be equal.
    KindSet tags = static_cast<KindSet>(b.decl_kinds & ~kKindInt);
    if (b.decl_kinds & kKindInt) tags |= b.decl_text_double ? kKindFrac : kKindInt;
    if (b.decl_may_double) tags |= kKindFrac;  // "double" is the Frac bit here
    for (KindSet binding = 1; binding <= kKindObject; binding <<= 1)
        for (KindSet v = 1; v <= kKindObject; v <<= 1)
            if ((tags & binding) && (value & v) && binding != v && !(v == kKindInt && binding == kKindFrac))
                return true;
    return false;
}

bool UnitCompiler::may_double(const TextType& type, std::size_t scope) const {
    switch (type.kind) {
        case TextType::Kind::Known: return type.known == 3;
        case TextType::Kind::Binding: return may_double_[scope].count(type.binding) != 0;
        default: return may_double(*type.left, scope) || may_double(*type.right, scope);
    }
}

// Legacy's textual path types a binding or parameter from the text, its
// prepared path from the value: the two differ only when the text gives a
// double type (or another binding's type that may be double) while the
// value is not a non-integral number.
bool UnitCompiler::type_mismatch_possible(const TextType& type, KindSet kinds, std::size_t scope) const {
    if (unknown_defers(kinds)) return false;
    if (kinds & kKindUnknown) kinds = kKindAll;  // may be anything (review D1)
    return may_double(type, scope) && (kinds & static_cast<KindSet>(kKindAll & ~kKindFrac)) != 0;
}

// Names (per scope) whose binding type may be double: declared from a text
// typed double or from a value that may be non-integral; parameters whose
// argument at some call may be such. A name-level over-approximation, to a
// fixpoint over the call graph.
void UnitCompiler::compute_may_double() {
    const std::size_t count = plan_.functions.size();
    may_double_.assign(count + 1, {});
    // Names every declaration of which (in its scope) is typed double by its
    // text: their binding type is always double (parameters excluded).
    always_double_.assign(count + 1, {});
    for (std::size_t scope = 0; scope <= count; ++scope) {
        std::unordered_set<std::string_view> other;
        if (scope < count) {
            const Node& fn = ast_.at(plan_.functions[scope]);
            for (std::uint32_t k = 0; k < fn.count; ++k) other.insert(text(ast_.at(ast_.list(fn)[k])));
        }
        std::vector<NodeId> stack{scope < count ? ast_.at(plan_.functions[scope]).b : root_};
        while (!stack.empty()) {
            const NodeId id = stack.back();
            stack.pop_back();
            if (id == kNoNode) continue;
            const Node& n = ast_.at(id);
            if (n.kind == NodeKind::FnDecl && scope == count) continue;
            if (n.kind == NodeKind::Declare && ast_.at(n.a).kind == NodeKind::Ident) {
                const std::string_view name = text(ast_.at(n.a));
                const TextType type = n.b != kNoNode ? text_type_at(n.b) : TextType();
                if (n.b != kNoNode && type.kind == TextType::Kind::Known && type.known == 3) {
                    if (!other.count(name)) always_double_[scope].insert(name);
                } else {
                    other.insert(name);
                    always_double_[scope].erase(name);
                }
            }
            const NodeId* list = ast_.list(n);
            for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
            for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
        }
    }
    const auto body_of = [&](std::size_t scope) {
        return scope < count ? ast_.at(plan_.functions[scope]).b : root_;
    };
    for (bool changed = true; changed;) {
        changed = propagate_global_doubles();  // CP12b: a global's verdicts are main's
        for (std::size_t scope = 0; scope <= count; ++scope) {
            std::vector<NodeId> stack{body_of(scope)};
            while (!stack.empty()) {
                const NodeId id = stack.back();
                stack.pop_back();
                if (id == kNoNode) continue;
                const Node& n = ast_.at(id);
                if (n.kind == NodeKind::FnDecl && scope == count) continue;  // main: skip function bodies
                if (n.kind == NodeKind::Declare && n.b != kNoNode && ast_.at(n.a).kind == NodeKind::Ident) {
                    const bool dbl = may_double(text_type_at(n.b), scope) ||
                                     (kinds(n.b) & (kKindFrac | kKindUnknown));
                    if (dbl && may_double_[scope].insert(text(ast_.at(n.a))).second) changed = true;
                }
                const NodeId* list = ast_.list(n);
                if (n.kind == NodeKind::Call && ast_.at(n.a).kind == NodeKind::Ident) {
                    const auto fn = plan_.index.find(text(ast_.at(n.a)));
                    if (fn != plan_.index.end()) {
                        const std::size_t g = fn->second - 1u;
                        const Node& callee = ast_.at(plan_.functions[g]);
                        for (std::uint32_t k = 0; k < n.count && k < callee.count; ++k) {
                            const bool dbl = may_double(text_type_at(list[k]), scope) ||
                                             (kinds(list[k]) & (kKindFrac | kKindUnknown));
                            const std::string_view param = text(ast_.at(ast_.list(callee)[k]));
                            if (dbl && may_double_[g].insert(param).second) changed = true;
                        }
                    }
                }
                for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
                for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
            }
        }
    }
}

bool UnitCompiler::compile_program(NodeId root, Program& program) {
    program_ = &program;
    lambda_tasks_.clear();
    prepare_captures();
    program.protos.clear();
    program.protos.resize(plan_.functions.size() + 1);
    // Lambda tasks append Protos while a main/function Proto is being built;
    // compile into a local first so a vector reallocation cannot invalidate
    // the output reference.
    std::unique_ptr<Proto> main_proto;
    if (!compile_main(root, main_proto)) return false;
    program.protos[0] = std::move(main_proto);
    for (std::size_t f = 0; f < plan_.functions.size(); ++f) {
        std::unique_ptr<Proto> proto;
        if (!compile_function(f, proto)) return false;
        program.protos[f + 1] = std::move(proto);
    }
    for (std::size_t i = 0; i < lambda_tasks_.size(); ++i) {
        const LambdaTask task = lambda_tasks_[i];
        std::unique_ptr<Proto> proto;
        if (!compile_lambda_task(task, proto)) return false;
        program.protos[task.proto_slot] = std::move(proto);
    }
    program_ = nullptr;
    return true;
}

bool UnitCompiler::compile_function(std::size_t f, std::unique_ptr<Proto>& out) {
    in_function_ = true;
    in_lambda_ = false;
    function_ = f;
    const Node& node = ast_.at(plan_.functions[f]);
    begin(std::string(text(ast_.at(node.a))), node.b, &assigned_fn_[f]);
    const NodeId* params = ast_.list(node);
    // CP12b structs: a method's first register holds its instance's stack slot.
    const bool method = plan_.is_method(f);
    if (method && !fb_->alloc(this_reg_)) return builder_ok();
    std::vector<std::uint16_t> regs(node.count);
    for (std::uint32_t k = 0; k < node.count; ++k)
        if (!fb_->alloc(regs[k])) return builder_ok();
    fb_->set_params(static_cast<std::uint16_t>(node.count + (method ? 1 : 0)));
    std::vector<std::uint16_t> type_regs(node.count);
    for (std::uint32_t k = 0; k < node.count; ++k) {
        const Node& param = ast_.at(params[k]);
        const std::string_view name = text(param);
        if (plan_.index.count(name)) return fail(param, "param-shadows-fn");
        Binding binding;
        binding.reg = regs[k];
        binding.decl_node = node_id(param);
        binding.typed = true;
        binding.decl_kinds = kinds(params[k]);
        binding.decl_may_double = may_double_[f].count(name) != 0;
        if (!fb_->alloc(binding.type_reg)) return builder_ok();
        type_regs[k] = binding.type_reg;
        fb_->set_offset(param.begin);
        // Canonical P5: a parameter is typed from the argument's text and
        // value (bind_call_param); the caller leaves the text type in the
        // parameter's type register (pass_param_types) and BindType combines
        // it with the value. Where legacy's prepared path (value only) could
        // differ, the call is refused (context:parameter-type).
        std::uint16_t text_type = 0;
        if (observable_[f][k]) text_type = binding.type_reg;
        else if (!fb_->constant(Value::integer(-1), text_type)) return builder_ok();
        fb_->emit(OpCode::BindType, binding.type_reg, binding.reg, text_type);
        if (captured_binding(node_id(param)) || selector_roots_.count(node_id(param))) {
            binding.selector_root_only = !captured_binding(node_id(param));
            binding.location_source = selector_roots_.count(binding.decl_node);
            if (!fb_->alloc(binding.cell_reg)) return builder_ok();
            const auto op = binding.location_source ? OpCode::NewSourceSlot : OpCode::NewCell;
            fb_->emit(op, binding.cell_reg, binding.reg, binding.type_reg);
            if (binding.location_source) fb_->emit(OpCode::LoadNull, binding.reg);
            binding.cell = true;
            binding.reg = binding.cell_reg;
        }
        if (!scopes_.declare(name, binding)) return fail(param, "param-duplicate");
    }
    // The body shares the parameters' scope: `p := ...` redeclares a parameter.
    if (!statements(ast_.at(node.b))) return false;
    fb_->emit(OpCode::ReturnNull);
    fb_->drop_unread_types();
    if (!finish(out)) return false;
    // A parameter type read in the body that the analysis did not predict
    // would not be passed by the callers: refuse instead.
    for (const Instr& in : out->code)
        for (std::uint32_t k = 0; k < node.count; ++k)
            if (in.op == OpCode::BindType && in.a == type_regs[k] && !observable_[f][k] &&
                !captured_binding(node_id(ast_.at(params[k]))) &&
                !selector_roots_.count(node_id(ast_.at(params[k]))))
                return fail(ast_.at(params[k]), "param-type-unpredicted");
    return true;
}

} // namespace nift::rt::detail
