#include "rt/compiler_unit.h"

#include <unordered_set>

// The unit's function variants (CP12): a function legacy runs on both of its
// evaluators is compiled once per evaluator, on demand, so the evaluator of
// every instruction is fixed before execution; and the parameters whose
// textual (P5) types a textual call passes.

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
void typed_names(const Ast& ast, const UnitPlan& plan, NodeId body, std::unordered_set<std::string_view>& out,
                 std::vector<ArgUse>& args) {
    collect_assigned(ast, body, out);
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

UnitCompiler::UnitCompiler(const Ast& ast, const UnitPlan& plan, const UnitKinds& kinds)
    : ast_(ast), plan_(plan), kinds_(kinds) {
    const std::size_t count = plan.functions.size();
    variant_proto_.assign(count, {0, 0});
    observable_.resize(count);
    std::vector<std::unordered_set<std::string_view>> names(count);
    std::vector<std::vector<ArgUse>> args(count);
    for (std::size_t f = 0; f < count; ++f) {
        typed_names(ast, plan, ast.at(plan.functions[f]).b, names[f], args[f]);
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
}

std::uint16_t UnitCompiler::proto_for(std::size_t f, Variant variant) {
    std::uint16_t& index = variant_proto_[f][static_cast<std::size_t>(variant)];
    if (index == 0) {
        index = next_proto_++;
        pending_.emplace_back(f, variant);
    }
    return index;
}

bool UnitCompiler::compile_program(NodeId root, Program& program) {
    program.protos.clear();
    program.protos.emplace_back();
    if (!compile_main(root, program.protos[0])) return false;
    for (int round = 0; round < 2; ++round) {
        while (!pending_.empty()) {
            const auto [f, variant] = pending_.front();
            pending_.pop_front();
            if (next_proto_ >= 0x7ff0) return fail(ast_.at(root), "too-many-functions");
            std::unique_ptr<Proto> proto;
            if (!compile_function(f, variant, proto)) return false;
            const std::uint16_t index = variant_proto_[f][static_cast<std::size_t>(variant)];
            if (program.protos.size() <= index) program.protos.resize(index + 1u);
            program.protos[index] = std::move(proto);
        }
        // Functions no compiled call reaches are still compiled (and so
        // checked) once, as textual variants; they never run.
        for (std::size_t f = 0; f < plan_.functions.size(); ++f)
            if (!variant_proto_[f][0] && !variant_proto_[f][1]) proto_for(f, Variant::Textual);
    }
    return true;
}

bool UnitCompiler::compile_function(std::size_t f, Variant variant, std::unique_ptr<Proto>& out) {
    in_function_ = true;
    variant_ = variant;
    const Node& node = ast_.at(plan_.functions[f]);
    begin(std::string(text(ast_.at(node.a))), node.b);
    const NodeId* params = ast_.list(node);
    std::vector<std::uint16_t> regs(node.count);
    for (std::uint32_t k = 0; k < node.count; ++k)
        if (!fb_->alloc(regs[k])) return builder_ok();
    fb_->set_params(static_cast<std::uint16_t>(node.count));
    std::vector<std::uint16_t> type_regs(node.count);
    for (std::uint32_t k = 0; k < node.count; ++k) {
        const Node& param = ast_.at(params[k]);
        const std::string_view name = text(param);
        if (plan_.index.count(name)) return fail(param, "param-shadows-fn");
        Binding binding;
        binding.reg = regs[k];
        binding.typed = true;
        if (!fb_->alloc(binding.type_reg)) return builder_ok();
        type_regs[k] = binding.type_reg;
        fb_->set_offset(param.begin);
        // Legacy types a parameter from the argument's value on the prepared
        // path and from the argument's text on the textual path (P5): a
        // textual call leaves the text type in the parameter's type register
        // (pass_param_types) and BindType combines it with the value.
        std::uint16_t text_type = 0;
        if (variant == Variant::Textual && observable_[f][k]) text_type = binding.type_reg;
        else if (!fb_->constant(Value::integer(-1), text_type)) return builder_ok();
        fb_->emit(OpCode::BindType, binding.type_reg, binding.reg, text_type);
        if (!scopes_.declare(name, binding)) return fail(param, "param-duplicate");
    }
    // The body shares the parameters' scope: `p := ...` redeclares a parameter.
    if (!statements(ast_.at(node.b))) return false;
    fb_->emit(OpCode::ReturnNull);
    fb_->drop_unread_types();
    if (!finish(out)) return false;
    // A parameter type read in the body that the analysis did not predict
    // would be typed from the value on a textual call: refuse instead.
    if (variant == Variant::Textual)
        for (const Instr& in : out->code)
            for (std::uint32_t k = 0; k < node.count; ++k)
                if (in.op == OpCode::BindType && in.a == type_regs[k] && !observable_[f][k])
                    return fail(ast_.at(params[k]), "param-type-unpredicted");
    return true;
}

} // namespace nift::rt::detail
