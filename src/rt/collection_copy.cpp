#include "rt/collection_copy.h"
#include "rt/collection.h"
#include <unordered_set>
#include <utility>
#include <vector>
// rt-lint: cold-file compile-time minimum D-040 binding-copy admission
namespace nift::rt {
bool collection_binding_copy(const Ast& ast,const UnitKinds& kinds,const Node& copy) {
    if(copy.kind!=NodeKind::Declare&&copy.kind!=NodeKind::Assign)return false;
    auto decl=[&](NodeId id){const auto found=kinds.bindings.find(id);
        return found==kinds.bindings.end()?id:found->second;};
    std::unordered_set<NodeId> needed;
    std::vector<std::pair<NodeId,NodeId>> edges;
    for(NodeId id=0;id<ast.size();++id) {
        const Node& n=ast.at(id);
        if((n.kind==NodeKind::Declare||n.kind==NodeKind::Assign)&&n.a!=kNoNode&&n.b!=kNoNode&&
           ast.at(n.a).kind==NodeKind::Ident&&ast.at(n.b).kind==NodeKind::Ident)
            edges.emplace_back(decl(n.a),decl(n.b));
        if(n.kind!=NodeKind::Call||ast.at(n.a).kind!=NodeKind::Member)continue;
        const Node& member=ast.at(n.a);
        if(ast.at(member.a).kind==NodeKind::Ident&&collection_method(ast.text(ast.at(member.b)))>=0)
            needed.insert(decl(member.a));
    }
    bool grew=true;
    while(grew) {grew=false;for(const auto& edge:edges)
        if(needed.count(edge.first)||needed.count(edge.second)) {
            grew=needed.insert(edge.first).second||grew;
            grew=needed.insert(edge.second).second||grew;
        }
    }
    return needed.count(decl(copy.a))||needed.count(decl(copy.b));
}
}
