#include "rt/infer_collection.h"
#include "rt/collection.h"
// rt-lint: cold-file compile-time collection callable-flow inference
namespace nift::rt::infer_detail {
namespace {
TyRef array_type(const TyRef& elem){auto t=std::make_shared<Ty>();t->kinds=kKindArray;
    t->collection_array=true;t->elem=elem;
    t->tuple=elem&&elem->kinds==0&&!elem->any;return t;}
TyRef collection_value(const TyRef& value){
    if(!(value->kinds&kKindAggregate))return value;
    auto t=std::make_shared<Ty>(*value);
    t->collection_array=(t->kinds&kKindArray)!=0;t->collection_object=(t->kinds&kKindObject)!=0;
    return t;
}
TyRef object_type(const TyRef& elem){auto t=std::make_shared<Ty>();t->kinds=kKindObject;
    t->collection_object=true;t->elem=elem;return t;}
}
TyRef Inference::collection_callables(const TyRef& callee,const std::vector<TyRef>& values,bool& grew) {
    std::vector<TyRef> arguments;arguments.reserve(values.size());
    for(const TyRef& value:values)arguments.push_back(collection_value(value));
    const TyRef normal=call_callables(callee,arguments,grew);
    if(callee->any||(callee->kinds&kKindUnknown))return normal;
    TyRef out=empty_ty();
    for(int unit:callee->callables) {
        if(unit>=0&&static_cast<std::size_t>(unit)<plan_.functions.size())out=unite(out,returns_[unit]);
        else if(unit>static_cast<int>(plan_.functions.size())) {
            const auto& info=lambdas_[unit-plan_.functions.size()-1];
            out=unite(out,(ast_.at(info.node).flags&kFlagBlockBody)?info.collection_returns:info.returns);
        }
    }
    return out;
}
TyRef Inference::collection_result(const Node& n,const TyRef& recv,const std::vector<TyRef>& values,int id) {
    using M=CollectionMethod;const auto m=static_cast<M>(id);
    TyRef element=element_of(recv,nullptr,false);
    if(element->kinds==0&&!(recv->tuple&&recv->items.empty()))element=any_ty();
    const NodeId receiver_id=ast_.at(n.a).a;
    result_.collection_elements[receiver_id]=element->kinds;
    if(element->legacy_text_number)result_.collection_text_numbers.insert(receiver_id);
    else result_.collection_text_numbers.erase(receiver_id);
    if(m<M::Map) {
        const auto receiver = ast_.at(n.a).a;
        if (m == M::Push && !values.empty()) add_element(receiver, values[0]);
        if (m == M::Insert && values.size() > 1) add_element(receiver, values[1]);
        if (m == M::Splice && values.size() > 2) add_element(receiver, element_of(values[2], nullptr, false));
        if (session_ && m != M::Push && m != M::Insert && !(m == M::Splice && values.size() > 2))
            add_element(receiver, empty_ty());  // any mutation invalidates positional facts
        if (m == M::Sort && !values.empty()) {
            bool grew = false;
            const auto argument=collection_value(element);
            const TyRef order=call_callables(values[0], {argument, argument}, grew);
            if (plan_.collections && (order->kinds & kKindCollection)) result_.collection_inspections.insert(n.a);  // D-063
        }
        // D-063: a plain sort compares the elements (legacy: tag strings).
        if (m == M::Sort && values.empty() && plan_.collections && (element->kinds & kKindCollection))
            result_.collection_inspections.insert(n.a);
        if(m==M::Push||m==M::Insert){const std::size_t at=m==M::Push?0:1;
            return collection_value(at<values.size()?values[at]:any_ty());}
        if(m==M::Pop||m==M::Remove)return collection_value(element);
        if(m==M::Clear)return kinds_ty(kKindNull);
        return array_type(element);
    }
    for (std::size_t k=0; k<values.size(); k+=(m==M::SortBy&&values.size()>1?2:1)) {
        if(m==M::Reduce&&k>0)break;
        for(int unit:values[k]->callables)
            if(unit>static_cast<int>(plan_.functions.size())) {
                const NodeId lambda=lambdas_[unit-plan_.functions.size()-1].node;
                if(ast_.at(lambda).count==(m==M::Reduce?2u:1u))result_.collection_lambdas.insert(lambda);
            }
    }
    TyRef result=empty_ty();
    if(!values.empty()) {
        bool grew=false;
        if(m==M::SortBy){for(std::size_t k=0;k<values.size();k+=(values.size()==1?1:2))result=unite(result,
            collection_callables(values[k],{element},grew));}
        else if(m==M::Reduce&&values.size()>1){result=collection_callables(values[0],{values[1],element},grew);
            result=unite(result,values[1]);bool ignored=false;result=unite(result,
            collection_callables(values[0],{result,element},ignored));}
        else result=collection_callables(values[0],{element},grew);
    }
    // D-063: the key/selector callbacks' results are compared, hashed or
    // checked as keys (legacy: a collection result is its tag string).
    const bool keyed_callback=m==M::SortBy||m==M::GroupBy||m==M::IndexBy||m==M::UniqueBy||m==M::MinBy||
                              m==M::MaxBy||m==M::CountBy||m==M::GroupByEach;
    // group_by_each's callback returns an array of keys.
    const KindSet keys=m==M::GroupByEach?(result->kinds|element_of(result,nullptr,false)->kinds):result->kinds;
    if(keyed_callback&&plan_.collections&&(keys&kKindCollection))result_.collection_inspections.insert(n.a);
    switch(m) {
        case M::Map:return array_type(result);
        case M::Filter:case M::SortBy:case M::UniqueBy:return array_type(element);
        case M::Reduce:return collection_value(result);
        case M::Any:case M::All:return kinds_ty(kKindBool);
        case M::Find:return collection_value(unite(element,kinds_ty(kKindNull)));
        case M::FindIndex:case M::Count:return kinds_ty(kKindInt);
        case M::MinBy:case M::MaxBy:return collection_value(element);
        case M::Partition:{auto t=std::make_shared<Ty>();t->kinds=kKindObject;t->collection_object=true;
            t->fields["matched"]=array_type(element);t->fields["unmatched"]=array_type(element);return t;}
        case M::IndexBy:return object_type(element);
        case M::CountBy:return object_type(kinds_ty(kKindInt));
        case M::GroupBy:case M::GroupByEach:return object_type(array_type(element));
        default:return any_ty();
    }
}
// D-063: map/set/stack methods. A store through a bare binding widens that
// binding's element type (map values, stack elements); with value semantics
// (copies never share) the per-binding element type is sound. Set members are
// scalars by legacy's rule (any other value is a run-time error).
// The members of a set / the keys of a map: what was stored (any kind when
// the collection's contents are unknown).
TyRef Inference::set_members(const TyRef& set) const {
    if (set->any) return (set->kinds & kKindUnknown) ? any_ty() : top_ty();
    return set->elem ? set->elem : empty_ty();
}
TyRef Inference::map_keys(const TyRef& map) const {
    if (map->any) return (map->kinds & kKindUnknown) ? any_ty() : top_ty();
    return map->key ? map->key : empty_ty();
}
TyRef Inference::keyed_result(const Node& n,const TyRef& recv,const std::vector<TyRef>& values,
                              std::string_view name) {
    const NodeId receiver=ast_.at(n.a).a;
    const auto element=[&]{TyRef e=element_of(recv,nullptr,false);

        return e;};
    if(name=="stringify"||name=="prettify"||name=="highlight")return kinds_ty(kKindString);
    if(name=="size")return kinds_ty(kKindInt);
    if(name=="empty"||name=="contains")return kinds_ty(kKindBool);
    if(name=="clear")return kinds_ty(kKindNull);
    if(recv->kinds==kKindMap) {
        if(name=="set"){if(values.size()==2){add_element(receiver,values[0],true);add_element(receiver,values[1]);
            return values[1];}return any_ty();}
        if(name=="get"||name=="remove")return element();
    } else if(recv->kinds==kKindSet) {
        if(name=="add"&&values.size()==1){add_element(receiver,values[0]);return values[0];}
        if(name=="remove"&&values.size()==1)return values[0];
    } else if(recv->kinds==kKindStack) {
        if(name=="push"){if(values.size()==1){add_element(receiver,values[0]);return values[0];}return any_ty();}
        if(name=="pop"||name=="top"||name=="front")return element();
    }
    return any_ty();
}
} // namespace nift::rt::infer_detail
