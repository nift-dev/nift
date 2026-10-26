#include "rt/infer_record.h"
// rt-lint: cold-file compile-time expression kind recording
namespace nift::rt::infer_detail {
TyRef Inference::record(NodeId id, TyRef t) {
    result_.kinds[id] = t->kinds;
    if(ast_.at(id).kind==NodeKind::Ident) {
        if(TyRef* binding=find(ast_.text(ast_.at(id)))) {
            const auto slot=static_cast<std::size_t>(binding-vars_.data());
            const auto decl=binding_decls_.find(slot);
            if(decl!=binding_decls_.end())result_.bindings[id]=decl->second;
        }
    }
    if(t->collection_array)result_.collection_arrays.insert(id);
    else result_.collection_arrays.erase(id);
    if(t->collection_object)result_.collection_objects.insert(id);
    else result_.collection_objects.erase(id);
    if(plan_.coroutines&&holds_future(t))result_.future_holders.insert(id);  // CP22
    else result_.future_holders.erase(id);
    auto& targets = result_.callable_targets[id];
    targets.clear();
    result_.callable_unknown[id] = t->any || (t->kinds & kKindUnknown);
    for (int unit : t->callables) {
        if (unit >= 0 && static_cast<std::size_t>(unit) < plan_.functions.size())
            targets.push_back(plan_.functions[unit]);
        else if (unit > static_cast<int>(plan_.functions.size()))
            targets.push_back(lambdas_[unit - plan_.functions.size() - 1].node);
        else result_.callable_unknown[id] = true;
    }
    return t;
}

}
