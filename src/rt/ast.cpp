#include "rt/ast.h"

#include <utility>

namespace nift::rt {

NodeId Ast::add(NodeKind kind, std::uint32_t begin, std::uint32_t end) {
    Node node;
    node.kind = kind;
    node.begin = begin;
    node.end = end;
    nodes_.push_back(node);
    return static_cast<NodeId>(nodes_.size() - 1);
}

void Ast::set_list(NodeId id, const std::vector<NodeId>& children) {
    Node& node = nodes_[id];
    node.list = static_cast<std::uint32_t>(lists_.size());
    node.count = static_cast<std::uint32_t>(children.size());
    lists_.insert(lists_.end(), children.begin(), children.end());
}

// Iterative (explicit stack): tree depth is bounded by the parser, but tools
// must not depend on that for their own stack use.
std::size_t Ast::opaque_count(NodeId id) const {
    std::size_t total = 0;
    std::vector<NodeId> stack{id};
    while (!stack.empty()) {
        const NodeId next = stack.back();
        stack.pop_back();
        if (next == kNoNode) continue;
        const Node& node = nodes_[next];
        total += node.kind == NodeKind::Opaque ? 1 : 0;
        stack.insert(stack.end(), {node.a, node.b, node.c});
        stack.insert(stack.end(), lists_.begin() + node.list, lists_.begin() + node.list + node.count);
    }
    return total;
}

const char* node_kind_name(NodeKind kind) {
    static const char* const names[] = {
        "Program", "Block", "ExprStmt", "Declare", "Assign", "CompoundAssign", "IncDec", "If", "While", "For",
        "ForBinding", "FnDecl", "Param", "StructDecl", "StructField", "EnumDecl", "Import", "Export", "Return",
        "Throw", "Break", "Continue", "Try", "Command", "TemplateStmt", "Comment", "Pattern",
        "Number", "String", "StringPart", "True", "False", "Null", "Ident", "Unary", "Binary", "Logical",
        "Coalesce", "Ternary", "Call", "Member", "SafeMember", "Index", "SafeIndex", "Array", "Object",
        "Property", "Lambda", "Spread", "Await", "Opaque"};
    return names[static_cast<int>(kind)];
}

const char* op_name(Op op) {
    static const char* const names[] = {
        "", "neg", "pos", "!", "+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=", "&&", "||",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "++pre", "--pre", "++post", "--post"};
    return names[static_cast<int>(op)];
}

const char* opaque_reason_name(OpaqueReason reason) {
    static const char* const names[] = {
        "", "legacy-runtime-error", "irregular-legacy-parse", "template-text", "struct-body", "deferred-translation",
        "nesting-limit"};
    return names[static_cast<int>(reason)];
}

namespace {

bool shows_text(NodeKind kind) {
    switch (kind) {
    case NodeKind::Ident: case NodeKind::Number: case NodeKind::String: case NodeKind::StringPart:
    case NodeKind::Param: case NodeKind::Opaque: case NodeKind::Command: case NodeKind::TemplateStmt:
    case NodeKind::Comment: case NodeKind::Property:
        return true;
    default:
        return false;
    }
}

void dump_node(const Ast& ast, NodeId id, std::size_t depth, std::string& out) {
    const Node& node = ast.at(id);
    out.append(depth * 2, ' ');
    out += node_kind_name(node.kind);
    if (node.op != Op::None) { out += ' '; out += op_name(node.op); }
    if (node.flags) { out += " flags="; out += std::to_string(node.flags); }
    if (node.kind == NodeKind::Opaque) { out += " ["; out += opaque_reason_name(node.reason); out += ']'; }
    out += " @" + std::to_string(node.begin) + ".." + std::to_string(node.end);
    if (shows_text(node.kind)) {
        std::string text(ast.text(node));
        if (text.size() > 60) text = text.substr(0, 57) + "...";
        for (char& ch : text) if (ch == '\n') ch = ' ';
        out += " `" + text + '`';
    }
    out += '\n';
}

} // namespace

// Pre-order, children a, b, c then the list; iterative (explicit stack).
std::string dump(const Ast& ast, NodeId root) {
    std::string out;
    std::vector<std::pair<NodeId, std::size_t>> stack{{root, 0}};
    while (!stack.empty()) {
        const auto [id, depth] = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        dump_node(ast, id, depth, out);
        const Node& node = ast.at(id);
        for (std::uint32_t i = node.count; i-- > 0;) stack.push_back({ast.list(node)[i], depth + 1});
        for (NodeId child : {node.c, node.b, node.a}) stack.push_back({child, depth + 1});
    }
    return out;
}

} // namespace nift::rt
