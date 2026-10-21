#include "rt/structs.h"

#include "rt/literal.h"
#include "rt/native.h"
#include "rt/text_type.h"
#include "rt/vm_ops.h"

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt {

namespace {

bool refuse(UnitPlan& plan, const Node& at, const char* reason) {
    plan.ok = false;
    plan.reason = reason;
    plan.offset = at.begin;
    return false;
}

// The constant an initialiser denotes (a literal or a negated number).
bool constant(const Ast& ast, NodeId id, Value& out) {
    if (id == kNoNode) return false;
    const Node& n = ast.at(id);
    switch (n.kind) {
        case NodeKind::Number: return decode_number_literal(ast.text(n), out);
        case NodeKind::String: return n.count == 0 && decode_string_literal(ast.text(n), out);
        case NodeKind::True: out = Value::boolean(true); return true;
        case NodeKind::False: out = Value::boolean(false); return true;
        case NodeKind::Null: out = Value::null(); return true;
        case NodeKind::Unary:
            return n.op == Op::Neg && n.a != kNoNode && ast.at(n.a).kind == NodeKind::Number &&
                   decode_negative_number_literal(ast.text(ast.at(n.a)), out) == NegativeLiteral::Decoded;
        default: return false;
    }
}

bool admit_method(const Ast& ast, const Node& fn, UnitPlan& plan) {
    if (fn.flags & (kFlagAsync | kFlagDeferredBody)) return refuse(plan, fn, "struct-method");
    if (fn.a == kNoNode || fn.b == kNoNode) return refuse(plan, fn, "struct-method");
    const NodeId* params = ast.list(fn);
    for (std::uint32_t k = 0; k < fn.count; ++k) {
        const Node& param = ast.at(params[k]);
        if (param.kind != NodeKind::Param || (param.flags & kFlagVariadic)) return refuse(plan, param, "fn-param");
        if (reserved_name(ast.text(param)) || ast.text(param) == "this")  // rt-lint: allow-string-compare
            return refuse(plan, param, "param-name-reserved");
    }
    return true;
}

} // namespace

bool plan_structs(const Ast& ast, NodeId root, UnitPlan& plan) {
    const Node& program = ast.at(root);
    const NodeId* items = ast.list(program);
    // Struct declarations anywhere else than the top level are refused.
    std::vector<NodeId> stack;
    for (std::uint32_t k = 0; k < program.count; ++k) {
        const Node& st = ast.at(items[k]);
        if (st.kind == NodeKind::StructDecl) continue;
        stack.push_back(items[k]);
    }
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::StructDecl) return refuse(plan, n, "struct-nested");
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
    }
    bool executable_seen = false;
    for (std::uint32_t k = 0; k < program.count; ++k) {
        const Node& st = ast.at(items[k]);
        if (st.kind == NodeKind::FnDecl) continue;
        if (st.kind != NodeKind::StructDecl) { executable_seen = true; continue; }
        if (executable_seen) return refuse(plan, st, "struct-after-statement");
        const std::string_view name = ast.text(ast.at(st.a));
        const bool reserved = reserved_name(name) || name == "this";  // rt-lint: allow-string-compare
        if (reserved) return refuse(plan, st, "struct-name");
        if (plan.struct_index.count(name)) return refuse(plan, st, "struct-redeclared");  // L47
        if (plan.index.count(name)) return refuse(plan, st, "struct-shadows-fn");        // L48
        StructInfo info;
        info.name = name;
        info.decl = items[k];
        const NodeId* members = ast.list(st);
        for (std::uint32_t m = 0; m < st.count; ++m) {
            const Node& member = ast.at(members[m]);
            const std::string_view member_name = ast.text(ast.at(member.a));
            if (info.field(member_name) || info.method(member_name) || plan.index.count(member_name) ||
                member_name == "this")  // rt-lint: allow-string-compare compile time
                return refuse(plan, member, "struct-member-clash");
            if (member.kind == NodeKind::StructField) {
                StructFieldInfo field;
                field.name = member_name;
                field.node = members[m];
                field.priv = (member.flags & kFlagPrivate) != 0;
                if (!constant(ast, member.b, field.value)) return refuse(plan, member, "struct-field-init");
                const TextType text = infer_text_type(ast.text(ast.at(member.b)));
                field.type = text_binding_type(text.kind == TextType::Kind::Known ? text.known : -1, field.value);
                info.fields.push_back(std::move(field));
                continue;
            }
            if (member.kind != NodeKind::FnDecl || !admit_method(ast, member, plan)) {
                if (plan.ok) refuse(plan, member, "struct-method");
                return false;
            }
            StructMethodInfo method;
            method.name = member_name;
            method.priv = (member.flags & kFlagPrivate) != 0;
            method.function = plan.functions.size();
            if (member_name == name) info.ctor = static_cast<int>(info.methods.size());
            plan.functions.push_back(members[m]);
            plan.method_of.push_back(static_cast<int>(plan.structs.size()));
            info.methods.push_back(method);
        }
        plan.struct_index.emplace(name, plan.structs.size());
        plan.structs.push_back(std::move(info));
    }
    return true;
}

} // namespace nift::rt
