#include "rt/collection_effects.h"
#include "rt/collection.h"
#include <unordered_set>
#include <functional>
// rt-lint: cold-file compile-time transitive comparator effect admission
namespace nift::rt {
std::string_view collection_sort_effects(const Ast& ast,NodeId root,NodeId callback,
                                        NodeId receiver,const UnitKinds& kinds,bool invoke) {
    const auto receiver_binding=kinds.bindings.find(receiver);
    if(receiver_binding==kinds.bindings.end())return "unknown-effects";
    const NodeId source_decl=receiver_binding->second;
    std::unordered_set<NodeId> locations;
    bool fresh=false;
    std::vector<NodeId> todo{root};
    while(!todo.empty()) {
        NodeId id=todo.back();todo.pop_back();if(id==kNoNode)continue;
        const Node& n=ast.at(id);
        if(n.kind==NodeKind::ForBinding)for(std::uint32_t i=0;i<n.count;
            ++i)locations.insert(ast.list(n)[i]);
        for(NodeId child:{n.a,n.b,n.c})todo.push_back(child);
        for(std::uint32_t i=0;i<n.count;++i)todo.push_back(ast.list(n)[i]);
    }
    auto root_name=[&](NodeId id) {
        while(id!=kNoNode&&(ast.at(id).kind==NodeKind::Index||ast.at(id).kind==NodeKind::Member))id=ast.at(id).a;
        if(id==kNoNode||ast.at(id).kind!=NodeKind::Ident)return kNoNode;
        const auto binding=kinds.bindings.find(id);
        return binding==kinds.bindings.end()?kNoNode:binding->second;
    };
    std::unordered_set<std::uint64_t> visited;
    std::string_view failure;
    std::function<void(NodeId)> scan;
    std::function<void(NodeId)> target=[&](NodeId id) {
        const auto known=kinds.callable_targets.find(id);
        const auto value_kinds=kinds.of(id);
        if(value_kinds&&!(value_kinds&(kKindFunction|kKindUnknown)))return;
        const auto unknown=kinds.callable_unknown.find(id);
        if(known==kinds.callable_targets.end()||known->second.empty()||
           (unknown!=kinds.callable_unknown.end()&&unknown->second)){failure="unknown-target";return;}
        for(NodeId t:known->second) {
            const bool saved=fresh;
            const Node& callable=ast.at(t);
            if(callable.kind==NodeKind::FnDecl) {
                fresh=false;
                std::vector<NodeId> local{callable.b};
                for(std::uint32_t k=0;k<callable.count;++k)if(ast.list(callable)[k]==source_decl)fresh=true;
                while(!local.empty()) {
                    const auto d=local.back();local.pop_back();if(d==kNoNode)continue;
                    const Node& n=ast.at(d);
                    if(n.kind==NodeKind::Lambda||n.kind==NodeKind::FnDecl)continue;
                    if(n.kind==NodeKind::Declare&&n.a==source_decl)fresh=true;
                    for(NodeId child:{n.a,n.b,n.c})local.push_back(child);
                    for(std::uint32_t k=0;k<n.count;++k)local.push_back(ast.list(n)[k]);
                }
            } else if(ast.at(id).kind!=NodeKind::Lambda)fresh=false;
            const auto key=(static_cast<std::uint64_t>(t)<<1)|fresh;
            if(visited.insert(key).second)scan(callable.b);
            fresh=saved;
        }
    };
    scan=[&](NodeId id) {
        if(id==kNoNode||!failure.empty())return;
        const Node& n=ast.at(id);
        if(n.kind==NodeKind::Lambda||n.kind==NodeKind::FnDecl)return;
        if(n.kind==NodeKind::Assign||n.kind==NodeKind::CompoundAssign||n.kind==NodeKind::IncDec) {
            const auto name=root_name(n.a);
            if(name==source_decl&&!fresh){failure="receiver-write";return;}
            if(name==kNoNode||locations.count(name)){failure="unknown-effects";return;}
            // Writes to nonlocal locations and aggregate parameters are not
            // inferred from heap identity. A default-for location is opaque.
        }
        if(n.kind==NodeKind::Call) {
            for(std::uint32_t k=0;k<n.count;++k) {
                const NodeId arg=ast.list(n)[k];
                if(locations.count(root_name(arg))&&(kinds.of(arg)&(kKindAggregate|kKindFunction|kKindUnknown))) {
                    failure="location-escape";return;
                }
            }
            const Node& callee=ast.at(n.a);
            const auto found=kinds.callable_targets.find(n.a);
            if(found!=kinds.callable_targets.end()&&!found->second.empty())target(n.a);
            else if(callee.kind==NodeKind::Member) {
                const auto name=ast.text(ast.at(callee.b));
                const int method=collection_method(name);
                if(method>=0&&method<static_cast<int>(CollectionMethod::Map)) {
                    const auto place=root_name(callee.a);
                    if(place==source_decl&&!fresh){failure="self-structural";return;}
                    if(place==kNoNode||locations.count(place)){failure="unknown-effects";return;}
                    if(name=="sort"&&n.count)target(ast.list(n)[0]);
                } else if(method>=static_cast<int>(CollectionMethod::Map)) {
                    const auto* args=ast.list(n);
                    const std::uint32_t step=name=="sort_by"&&n.count>1?2:1;
                    const std::uint32_t count=name=="reduce"?std::min(1u,n.count):n.count;
                    for(std::uint32_t k=0;k<count;k+=step)target(args[k]);
                } else {
                    // The closed callback-free native scalar/read operations
                    // below cannot write a location or invoke user code.
                    static constexpr std::string_view reads[]={"size","join","stringify","prettify","highlight",
                        "to_string","abs","floor","ceil","round","contains","starts_with","ends_with","slice",
                        "take","drop","keys","values","has","get"};
                    bool read=false;for(auto item:reads)read=read||name==item;
                    if(!read||(kinds.of(n.a)&(kKindFunction|kKindUnknown))||
                       (kinds.of(callee.a)&kKindUnknown)) {
                        target(n.a);if(!failure.empty())return;
                    }
                }
            } else if(callee.kind==NodeKind::Ident) {
                const auto name=ast.text(callee);
                if(name!="print"&&name!="error"&&name!="type"&&name!="range")target(n.a);
            } else target(n.a);
        }
        for(NodeId child:{n.a,n.b,n.c})scan(child);
        for(std::uint32_t k=0;k<n.count;++k)scan(ast.list(n)[k]);
    };
    // The factory expression itself can have effects, independently of the
    // finite set of comparator values it returns.
    scan(callback);if(failure.empty()&&invoke)target(callback);
    return failure;
}
}
