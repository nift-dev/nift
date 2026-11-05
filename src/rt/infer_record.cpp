#include "rt/infer_record.h"
#include "rt/session_shape.h"
// rt-lint: cold-file compile-time expression kind recording
namespace nift::rt::infer_detail {
TyRef Inference::record(NodeId id, TyRef t) {
    const TyRef raw = t;  // the guard's own per-binding facts (B5 deletes them with the guard)
    t = sited(id, t);  // B4b: an array's or object's contents are its sites'
    if (!t->callables.empty() && !callee_nodes_.count(id))  // B4b C4: a function used as a value
        for (std::uint32_t unit : t->callables) value_units_.insert(unit);
    if (session_) t = session_current_shape(t);
    result_.kinds[id] = t->kinds;
    if (session_) result_.session_shapes[id] = t;
    if(ast_.at(id).kind==NodeKind::Ident) {
        if(TyRef* binding=find(ast_.text(ast_.at(id)))) {
            const auto slot=static_cast<std::size_t>(binding-vars_.data());
            const auto decl=binding_decls_.find(slot);
            if(decl!=binding_decls_.end())result_.bindings[id]=decl->second;
        }
    }
    if(raw->collection_array)result_.collection_arrays.insert(id);
    else result_.collection_arrays.erase(id);
    if(raw->collection_object)result_.collection_objects.insert(id);
    else result_.collection_objects.erase(id);
    if(plan_.coroutines&&holds_future(t))result_.future_holders.insert(id);  // CP22
    else result_.future_holders.erase(id);
    const bool tagged = plan_.collections || !plan_.structs.empty();  // D-063, D-NEXT-refsem
    if(tagged&&holds_collection(t))result_.collection_holders.insert(id);
    else result_.collection_holders.erase(id);
    if(tagged&&(t->kinds&(kKindArray|kKindObject))&&
       ((element_of(t,nullptr,false)->kinds|element_of(t,nullptr,true)->kinds)&kKindTagged))
        result_.collection_elements_of.insert(id);
    else result_.collection_elements_of.erase(id);
    if (t->kinds & kKindStruct) result_.struct_ids[id] = t->any ? -2 : t->struct_id;  // D-NEXT-refsem
    else result_.struct_ids.erase(id);
    if ((raw->kinds & (kKindArray | kKindObject)) && holds_aggregate_below(raw)) result_.nested_aggregates.insert(id);
    else result_.nested_aggregates.erase(id);
    auto& targets = result_.callable_targets[id];
    targets.clear();
    result_.callable_unknown[id] = t->any || (t->kinds & kKindUnknown) || t->untracked_callable;
    for (int unit : t->callables) {
        if (unit >= 0 && static_cast<std::size_t>(unit) < plan_.functions.size())
            targets.push_back(plan_.functions[unit]);
        else if (unit > static_cast<int>(plan_.functions.size()))
            targets.push_back(lambdas_[unit - plan_.functions.size() - 1].node);
        else result_.callable_unknown[id] = true;
    }
    return t;
}

// CP22: `thread(f, args…)` (legacy's thread built-in) in a coroutine unit: f
// is called by the task with the arguments; the value is a thread handle
// whose join() gives f's result (kept as its `elem`, like a future's).
TyRef Inference::thread_call(const Node& n) {
    const std::string_view name = ast_.text(ast_.at(n.a));
    if(name!="thread"||find(name)!=nullptr||plan_.index.find(name)!=plan_.index.end())return nullptr;
    const NodeId* args=ast_.list(n);
    const TyRef f=expr(args[0]);
    std::vector<TyRef> values;
    for(std::uint32_t k=1;k<n.count;++k)values.push_back(expr(args[k]));
    bool grew=false;
    const TyRef returns=f->callables.empty()?any_ty():call_callables(f,values,grew);
    auto handle=std::make_shared<Ty>();
    handle->kinds=kKindThread;
    handle->elem=returns;
    return handle;
}

}
