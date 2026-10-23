#include "rt/compiler_capture.h"

#include <string_view>
#include <utility>
#include <vector>

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt::detail {

namespace {

constexpr int kGlobalOwner = -2;
constexpr int kMainOwner = -1;

struct Decl {
    NodeId node = kNoNode;
    int owner = kMainOwner;
};

class Scanner {
public:
    Scanner(const Ast& ast, const std::unordered_set<NodeId>& globals, std::unordered_set<NodeId>& captured)
        : ast_(ast), globals_(globals), captured_(captured) {}

    void function(const Node& fn, int owner) {
        scopes_.clear();
        scopes_.emplace_back();
        const NodeId* params = ast_.list(fn);
        for (std::uint32_t k = 0; k < fn.count; ++k) add(ast_.text(ast_.at(params[k])), params[k], owner);
        node(fn.b, owner);
    }

    void program(const Node& root) {
        scopes_.clear();
        scopes_.emplace_back();
        const NodeId* list = ast_.list(root);
        for (std::uint32_t k = 0; k < root.count; ++k) {
            const Node& n = ast_.at(list[k]);
            if (n.kind == NodeKind::FnDecl || n.kind == NodeKind::StructDecl) continue;
            node(list[k], kMainOwner);
        }
    }

private:
    const Decl* find(std::string_view name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
            for (auto entry = scope->rbegin(); entry != scope->rend(); ++entry)
                if (entry->first == name) return &entry->second;
        return nullptr;
    }

    void add(std::string_view name, NodeId node, int owner) {
        scopes_.back().emplace_back(name, Decl{node, owner});
    }

    void lambda(const Node& lambda, int outer_owner) {
        const int owner = next_owner_++;
        (void)outer_owner;
        scopes_.emplace_back();
        const NodeId* params = ast_.list(lambda);
        for (std::uint32_t k = 0; k < lambda.count; ++k) add(ast_.text(ast_.at(params[k])), params[k], owner);
        node(lambda.b, owner);
        scopes_.pop_back();
    }

    void ident(const Node& n, int owner) {
        const Decl* decl = find(ast_.text(n));
        if (decl != nullptr && decl->owner != owner && decl->owner != kGlobalOwner) captured_.insert(decl->node);
    }

    void block(const Node& block, int owner) {
        scopes_.emplace_back();
        const NodeId* list = ast_.list(block);
        for (std::uint32_t k = 0; k < block.count; ++k) node(list[k], owner);
        scopes_.pop_back();
    }

    void node(NodeId id, int owner) {
        if (id == kNoNode) return;
        const Node& n = ast_.at(id);
        switch (n.kind) {
            case NodeKind::Ident: ident(n, owner); return;
            case NodeKind::Lambda: lambda(n, owner); return;
            case NodeKind::Declare: {
                node(n.b, owner);  // the initializer cannot see the new binding
                if (n.a != kNoNode && ast_.at(n.a).kind == NodeKind::Ident) {
                    const NodeId target = n.a;
                    const int decl_owner = globals_.count(target) ? kGlobalOwner : owner;
                    add(ast_.text(ast_.at(target)), target, decl_owner);
                }
                return;
            }
            case NodeKind::Param: return;
            case NodeKind::FnDecl: case NodeKind::StructDecl: case NodeKind::EnumDecl: return;
            case NodeKind::Block: block(n, owner); return;
            case NodeKind::If: {
                node(n.a, owner);
                node(n.b, owner);
                if (n.c != kNoNode) node(n.c, owner);
                return;
            }
            case NodeKind::While: {
                node(n.a, owner);
                node(n.b, owner);
                return;
            }
            default: break;
        }
        node(n.a, owner);
        node(n.b, owner);
        node(n.c, owner);
        const NodeId* list = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) node(list[k], owner);
    }

    const Ast& ast_;
    const std::unordered_set<NodeId>& globals_;
    std::unordered_set<NodeId>& captured_;
    std::vector<std::vector<std::pair<std::string_view, Decl>>> scopes_;
    int next_owner_ = 1 << 20;  // lambdas are distinct from function owners
};

} // namespace

void prepare_capture_bindings(const Ast& ast, NodeId root, const UnitPlan& plan,
                              std::unordered_set<NodeId>& top_level_decls,
                              std::unordered_set<NodeId>& captured) {
    top_level_decls.clear();
    captured.clear();
    const Node& program = ast.at(root);
    const NodeId* list = ast.list(program);
    for (std::uint32_t k = 0; k < program.count; ++k) {
        const Node& n = ast.at(list[k]);
        if (n.kind == NodeKind::Declare && n.a != kNoNode && ast.at(n.a).kind == NodeKind::Ident) {
            top_level_decls.insert(list[k]);
            top_level_decls.insert(n.a);
        }
    }
    Scanner scanner(ast, top_level_decls, captured);
    scanner.program(program);
    for (std::size_t f = 0; f < plan.functions.size(); ++f)
        scanner.function(ast.at(plan.functions[f]), static_cast<int>(f));
}

} // namespace nift::rt::detail
