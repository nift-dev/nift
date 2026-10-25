#include "rt/collection_helpers.h"
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/compare.h"
#include "rt/heap_account.h"
#include "rt/natives/support.h"
#include "rt/string.h"
#include <algorithm>
#include <cstring>
#include <numeric>
#include <vector>

namespace nift::rt {
namespace {
using M=CollectionMethod;
using namespace collection_slot;
bool error(Fault& f,std::string text){return raise(f,StopReason::Error,std::move(text));}
bool budget(const natives::Context& c,std::size_t bytes,Fault& f) {
    return natives::reserve_result(c,bytes)||raise(f,StopReason::Internal,"memory budget exceeded");
}
bool scalar_key(const Value& v){return v.is_number()||v.is(Kind::String)||v.is(Kind::Bool);}
bool key_order(const Value& a,const Value& b,int& cmp) {
    if(a.is_number()&&b.is_number()){cmp=natives::compare_numbers(a,b);return true;}
    if(a.is(Kind::String)&&b.is(Kind::String)){const auto x=string_view_of(a),y=string_view_of(b);
        cmp=x<y?-1:x>y?1:0;return true;}
    if(a.is(Kind::Bool)&&b.is(Kind::Bool)){cmp=int(a.as_bool())-int(b.as_bool());return true;}
    return false;
}
bool core(M m){return (m>=M::Map&&m<=M::Count)||m==M::GroupBy;}
bool generated_key(M m,const Value& value,Atom& key,Fault& f) {
    if(!scalar_key(value))return error(f,
        std::string(collection_names[static_cast<unsigned>(m)])+": generated key must be string, number, or bool");
    std::string rendered;natives::append_scalar(value,rendered);key=atom(rendered);return true;
}
void append_group(Value& object,Atom key,const Value& item) {
    if(!object_get(object,key))object_set(object,key,make_array());
    Value* group=object_slot_mut(object,key);array_push(*group,item);
}
void short_circuit(Value* s) {
#if !defined(NIFT_COLLECTION_MUTANT) || NIFT_COLLECTION_MUTANT != 6
    s[Index]=s[Length];
#else
    (void)s;
#endif
}
bool consume(M m,Value* s,const natives::Context&,Fault& f) {
    const Value& r=s[Returned];
    const std::string_view name=collection_names[static_cast<unsigned>(m)];
    switch(m) {
        case M::Map: array_push(s[Result],r);break;
        case M::Reduce:s[Result]=r;break;
        case M::Filter:case M::Any:case M::All:case M::Find:case M::FindIndex:case M::Count:case M::Partition: {
            #if !defined(NIFT_COLLECTION_MUTANT) || NIFT_COLLECTION_MUTANT != 5
            if(!r.is(Kind::Bool))return error(f,std::string(name)+": callback must return bool");
#endif
#if defined(NIFT_COLLECTION_MUTANT) && NIFT_COLLECTION_MUTANT == 5
            const bool yes=truthy(r);
#else
            const bool yes=r.as_bool();
#endif
            if(m==M::Filter&&yes)array_push(s[Result],s[Item]);
            else if(m==M::Any&&yes){s[Result]=Value::boolean(true);short_circuit(s);}
            else if(m==M::All&&!yes){s[Result]=Value::boolean(false);short_circuit(s);}
            else if((m==M::Find||m==M::FindIndex)&&yes){s[Result]=m==M::Find?s[Item]:s[Index];short_circuit(s);}
            else if(m==M::Count&&yes)s[Result]=Value::integer(s[Result].as_int()+1);
            else if(m==M::Partition)append_group(s[Result],atom(yes?"matched":"unmatched"),s[Item]);
            break;
        }
        case M::SortBy:
            if(!scalar_key(r))return error(f,"sort_by: key must be scalar");
            array_push(s[Keys],r);break;
        case M::MinBy:case M::MaxBy: {
            if(!scalar_key(r))return error(f,std::string(name)+": key must be scalar");
            if(s[Index].as_int()==0){s[Keys]=r;s[Result]=s[Item];break;}
            int cmp=0;if(!key_order(r,s[Keys],cmp))return error(f,
                std::string(name)+": keys must be comparable and homogeneous");
            if(m==M::MinBy?cmp<0:cmp>0){s[Keys]=r;s[Result]=s[Item];}break;
        }
        case M::UniqueBy: {
            auto& table=s[HashTable].heap_as<BytesObject>()->data;
            const std::size_t mask=table.size()/sizeof(std::uint64_t)-1;
            std::size_t slot=static_cast<std::size_t>(hash(r))&mask;
            std::uint64_t pos=0;
            for(;;slot=(slot+1)&mask){std::memcpy(&pos,table.data()+slot*sizeof(pos),sizeof(pos));if(!pos)break;
                if(equal(array_items(s[Keys])[pos-1],r))return true;}
            array_push(s[Keys],r);pos=array_size(s[Keys]);std::memcpy(table.data()+slot*sizeof(pos),&pos,
                sizeof(pos));array_push(s[Result],s[Item]);break;
        }
        case M::GroupBy:case M::IndexBy:case M::CountBy: {
            if(m==M::GroupBy&&!scalar_key(r))return error(f,"group_by: key must be scalar");
            Atom key;if(!generated_key(m,r,key,f))return false;
            if(m==M::GroupBy)append_group(s[Result],key,s[Item]);
            else if(m==M::IndexBy){if(object_get(s[Result],key))return error(f,
                "index_by: duplicate generated key '"+std::string(atom_text(key))+"'");object_set(s[Result],key,
                s[Item]);}
            else {const Value* count=object_get(s[Result],key);const std::int64_t n=count?count->as_int():0;
                object_set(s[Result],key,Value::integer(n+1));}
            break;
        }
        case M::GroupByEach: {
            if(!r.is(Kind::Array))return error(f,"group_by_each: selector must return an array");
            Value seen=make_object();
            for(const Value& k:array_items(r)) {Atom key;if(!generated_key(m,k,key,f))return false;
#if !defined(NIFT_COLLECTION_MUTANT) || NIFT_COLLECTION_MUTANT != 8
                if(object_get(seen,key))continue;
#endif
                object_set(seen,key,Value::boolean(true));
                append_group(s[Result],key,s[Item]);}break;
        }
        default:return raise(f,StopReason::Internal,"collection method");
    }
    return true;
}
bool sorted_keys(Value* s,const natives::Context& context,Fault& f) {
    const std::size_t n=static_cast<std::size_t>(s[Length].as_int()),width=array_size(s[Directions]);
    if(!budget(context,n*sizeof(Value),f))return false;
    s[HashTable]=make_array(n);
    for(std::size_t i=0;i<n;++i)array_push(s[HashTable],Value::integer(i));
    auto& order=array_mut(s[HashTable]);
    const auto& keys=array_items(s[Keys]);const auto& directions=array_items(s[Directions]);bool failed=false;
    std::sort(order.begin(),order.end(),[&](const Value& left,const Value& right){
        const auto a=static_cast<std::size_t>(left.as_int()),b=static_cast<std::size_t>(right.as_int());
        for(std::size_t k=0;k<width;++k){int cmp=0;
            const Value& x=keys[a*width+k];const Value& y=keys[b*width+k];
            if(!key_order(x,y,cmp)) {
                // Keep a strict ordering even on the diagnostic path. Later
                // keys are examined only when all earlier keys compare equal.
                failed=true;
                auto rank=[](const Value& v){return v.is_number()?0:v.is(Kind::Bool)?1:2;};
                cmp=rank(x)<rank(y)?-1:1;
            }
            if(cmp)return directions[k].as_bool()?cmp>0:cmp<0;
        }
#if defined(NIFT_COLLECTION_MUTANT) && NIFT_COLLECTION_MUTANT == 7
        return a>b;
#else
        return a<b;
#endif
    });
    if(failed)return error(f,"sort_by: keys must be comparable and homogeneous");
    for(const Value& i:order)array_push(s[Result],array_items(s[Source])[static_cast<std::size_t>(i.as_int())]);
    return true;
}
} // namespace

bool collection_step(CollectionMethod method,std::uint16_t phase,Value* s,const natives::Context& context,
    Fault& fault) {
    using namespace collection_slot;
    const std::string_view name=collection_names[static_cast<unsigned>(method)];
    if(phase==0) {
        if(!s[Source].is(Kind::Array))return raise(fault,StopReason::Internal,"collection receiver");
        const std::size_t n=array_size(s[Source]);s[Length]=Value::integer(n);s[Index]=Value::integer(0);
        if(core(method)&&!s[Callback].is(Kind::Function))return error(fault,
            "transformation: callback must be callable");
        if((method==M::MinBy||method==M::MaxBy)&&!n)return error(fault,
            std::string(name)+": cannot select from an empty array");
        if(method==M::Map||method==M::Filter||method==M::UniqueBy||method==M::SortBy){if(!budget(context,
            n*sizeof(Value)*2,fault))return false;
            s[Result]=make_array(n);}
        else if(method==M::All)s[Result]=Value::boolean(true);
        else if(method==M::Any)s[Result]=Value::boolean(false);
        else if(method==M::FindIndex)s[Result]=Value::integer(-1);
        else if(method==M::Count)s[Result]=Value::integer(0);
        else if(method==M::GroupBy||method==M::IndexBy||method==M::Partition||method==M::CountBy||
                method==M::GroupByEach)s[Result]=make_object();
        if(method==M::Partition){object_set(s[Result],atom("matched"),make_array());object_set(s[Result],
            atom("unmatched"),make_array());}
        if(method==M::SortBy){const std::size_t width=array_size(s[Directions]);
            if(width&&n>std::size_t(-1)/width/sizeof(Value))return raise(fault,StopReason::Internal,
            "memory budget exceeded");if(!budget(context,n*width*sizeof(Value),fault))return false;
            s[Keys]=make_array(n*width);}
        if(method==M::UniqueBy){std::size_t capacity=16;while(capacity<n*2)capacity*=2;if(!budget(context,
            capacity*sizeof(std::uint64_t)+n*sizeof(Value),fault))return false;
            s[Keys]=make_array(n);
            s[HashTable]=make_bytes(std::vector<std::uint8_t>(capacity*sizeof(std::uint64_t),0));}
        return true;
    }
    if(phase==1){const auto i=s[Index].as_int();const bool more=i<s[Length].as_int();
        s[More]=Value::boolean(more);if(more)s[Item]=array_items(s[Source])[static_cast<std::size_t>(i)];return true;}
    if(phase==2||phase==4){if(!consume(method,s,context,fault))return false;if(phase==2){
        s[Index]=Value::integer(s[Index].as_int()+1);s[Returned]=Value();s[Item]=Value();}return true;}
    if(phase==5){s[Index]=Value::integer(s[Index].as_int()+1);s[Returned]=Value();s[Item]=Value();return true;}
    if(phase==3)return method!=M::SortBy||sorted_keys(s,context,fault);
    return raise(fault,StopReason::Internal,"collection phase");
}
} // namespace nift::rt
