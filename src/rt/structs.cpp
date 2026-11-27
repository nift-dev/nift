#include "rt/structs.h"
#include "rt/aggregate.h"
#include "rt/binding_rules.h"

#include "rt/literal.h"
#include "rt/module_link.h"
#include "rt/compiler_process.h"
#include "rt/module_data.h"

#include <algorithm>
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
        case NodeKind::Call: {  // D-063: `map()`, `set()`, `stack()` (an empty value; copies never share)
            const std::string_view t = ast.text(n);
            if (n.count != 0) return false;
            if (t == "map()") out = make_collection(CollectionKind::Map);
            else if (t == "set()") out = make_collection(CollectionKind::Set);
            else if (t == "stack()") out = make_collection(CollectionKind::Stack);
            else return false;
            return true;
        }
        case NodeKind::Unary:
            return n.op == Op::Neg && n.a != kNoNode && ast.at(n.a).kind == NodeKind::Number &&
                   decode_negative_number_literal(ast.text(ast.at(n.a)), out) == NegativeLiteral::Decoded;
        default: return false;
    }
}

// These prefixes cannot invoke user code or observe a type before its
// declaration. Other initializers still close the struct declaration prefix.
bool module_prefix_data(const Ast& ast, const Node& node) {
    switch (node.kind) {
        case NodeKind::Number: case NodeKind::True: case NodeKind::False: case NodeKind::Null:
            return true;
        case NodeKind::String:
            return node.count == 0;
        case NodeKind::Binary:
            return node.op == Op::Add && module_prefix_data(ast, ast.at(node.a)) &&
                   module_prefix_data(ast, ast.at(node.b));
        case NodeKind::Array: {
            const auto* items = ast.list(node);
            return std::all_of(items, items + node.count, [&](NodeId id) {
                return module_prefix_data(ast, ast.at(id));
            });
        }
        case NodeKind::Call:
            break;
        default:
            return false;
    }
    if (module_ascii_literal(ast, node)) return true;
    const Node& callee = ast.at(node.a);
    return callee.kind == NodeKind::Ident && node.count == 0 &&
           ast.text(callee) == "map";  // rt-lint: allow-string-compare compile time
}

// The package uses args only as an argv source. Keep any receiver mutation,
// direct call, forwarding or data escape out of this name exception.
bool readonly_method_args(const Ast& ast, NodeId id, bool native_run) {
    if (id == kNoNode) return true;
    const Node& node = ast.at(id);
    const auto args_ident = [&](NodeId child) {
        return child != kNoNode && ast.at(child).kind == NodeKind::Ident &&
               ast.text(ast.at(child)) == "args";  // rt-lint: allow-string-compare compile time
    };
    if (args_ident(id)) return false;
    // Iterating the value (``for (arg : args)``) only reads it. The loop body
    // is a separate child of the For node and is still checked.
    if (node.kind == NodeKind::ForBinding && args_ident(node.a)) return true;
    if (node.kind == NodeKind::Call) {
        const Node& callee = ast.at(node.a);
        if (callee.kind == NodeKind::Member && args_ident(callee.a) && node.count == 0) {
            const auto name = ast.text(ast.at(callee.b));
            return name == "size" || name == "length";  // rt-lint: allow-string-compare compile time
        }
        const bool direct_run = callee.kind == NodeKind::Ident &&
                                ast.text(callee) == "run";  // rt-lint: allow-string-compare compile time
        const bool direct_cmd = callee.kind == NodeKind::Ident &&
                                ast.text(callee) == "cmd";  // rt-lint: allow-string-compare compile time
        if (native_run && (direct_run || direct_cmd) && detail::process_program_literal(ast, node)) {
            for (std::uint32_t k = 0; k < node.count; ++k) {
                const auto item = ast.list(node)[k];
                const Node& arg = ast.at(item);
                if (arg.kind == NodeKind::Spread && args_ident(arg.a)) continue;
                if (!readonly_method_args(ast, item, native_run)) return false;
            }
            return true;
        }
    }
    for (NodeId child : {node.a, node.b, node.c})
        if (!readonly_method_args(ast, child, native_run)) return false;
    for (std::uint32_t k = 0; k < node.count; ++k)
        if (!readonly_method_args(ast, ast.list(node)[k], native_run)) return false;
    return true;
}

bool admit_method(const Ast& ast, const Node& fn, UnitPlan& plan) {
    if (fn.flags & (kFlagAsync | kFlagDeferredBody)) return refuse(plan, fn, "struct-method");
    if (fn.a == kNoNode || fn.b == kNoNode) return refuse(plan, fn, "struct-method");
    const auto owner = plan.linked && plan.linked->linked ? plan.linked->locate(fn.begin).module : 0;
    const Node& body = ast.at(fn.b);
    const NodeId* params = ast.list(fn);
    for (std::uint32_t k = 0; k < fn.count; ++k) {
        const Node& param = ast.at(params[k]);
        const bool package_rest = k + 1 == fn.count && owner > 0;
        if (param.kind != NodeKind::Param || ((param.flags & kFlagVariadic) && !package_rest))
            return refuse(plan, param, "fn-param");
        const auto name = ast.text(param);
        // Local method values can be named args. A module with its own
        // generated invocation bindings still uses the conservative rule.
        bool method_args_value = false;
        if (owner > 0 && !(param.flags & kFlagVariadic) && name == "args" &&  // rt-lint: allow-string-compare
            plan.linked->graph.modules[owner]->generated_names.empty()) {
            bool native_run = true;
            for (NodeId id = 0; id < ast.size(); ++id) {
                const Node& candidate = ast.at(id);
                if ((candidate.kind == NodeKind::FnDecl || candidate.kind == NodeKind::Declare) &&
                    candidate.a != kNoNode && ast.text(ast.at(candidate.a)) == "run") { // rt-lint: allow-string-compare
                    native_run = false;
                    break;
                }
            }
            method_args_value = readonly_method_args(ast, fn.b, native_run);
        }
        const bool receiver_name = name == "this";  // rt-lint: allow-string-compare
        if (receiver_name || (!method_args_value && refused_parameter_name(ast, param, body)))
            return refuse(plan, param, "param-name-reserved");
    }
    return true;
}

} // namespace

bool plan_structs(const Ast& ast, NodeId root, UnitPlan& plan, const LinkedUnit* linked) {
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
    // An earlier statement may observe a declaration from its own module or
    // one of its dependencies. Independent earlier modules have no lexical
    // route to this type. Entry statements keep the original prefix fence.
    const bool linked_script = linked && linked->linked;
    std::vector<bool> blocked(linked_script ? linked->graph.modules.size() : 1, false);
    const auto seal = [&](std::size_t owner) {
        if (owner == 0) {
            std::fill(blocked.begin(), blocked.end(), true);
            return;
        }
        std::vector<std::size_t> pending{owner};
        while (!pending.empty()) {
            const auto module = pending.back();
            pending.pop_back();
            if (blocked[module]) continue;
            blocked[module] = true;
            for (const auto& edge : linked->graph.modules[module]->imports)
                pending.push_back(edge.module);
        }
    };
    for (std::uint32_t k = 0; k < program.count; ++k) {
        const Node& st = ast.at(items[k]);
        if (st.kind == NodeKind::FnDecl) continue;
        const std::size_t owner = linked_script ? linked->locate(st.begin).module : 0;
        if (st.kind != NodeKind::StructDecl) {
            const bool safe_module_data = owner > 0 && st.kind == NodeKind::Declare &&
                st.b != kNoNode && module_prefix_data(ast, ast.at(st.b));
            if (!safe_module_data) seal(owner);
            continue;
        }
        if (blocked[owner]) return refuse(plan, st, "struct-after-statement");
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
            method.params = member.count;
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

bool presentation_name(std::string_view name) {
    return name == "stringify" || name == "prettify" || name == "highlight";  // rt-lint: allow-string-compare
}

bool presentation_builtin_binds(const StructInfo& s, std::string_view name, std::uint32_t argc, bool inside) {
    return argc == 0 && presentation_name(name) && presentation_override(s, name, 0, inside) == nullptr;
}

const StructMethodInfo* presentation_override(const StructInfo& s, std::string_view name, std::uint32_t argc,
                                              bool inside) {
    if (argc != 0 || !presentation_name(name)) return nullptr;
    const StructMethodInfo* m = s.method(name);
    return m != nullptr && m->params == 0 && (!m->priv || inside) ? m : nullptr;
}

} // namespace nift::rt
