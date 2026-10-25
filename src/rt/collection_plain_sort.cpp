#include "rt/collection_plain_sort.h"
#include "rt/aggregate.h"
#include "rt/natives/support.h"
#include "rt/string.h"
#include <algorithm>
namespace nift::rt {
bool collection_plain_sort(Value* s,const natives::Context& context,Fault& fault) {
    using namespace collection_slot;
    const auto n=array_size(s[Source]);
    if(n<2){s[Result]=s[Source];return true;}
    const auto& input=array_items(s[Source]);
    const bool numbers=input[0].is_number(),strings=input[0].is(Kind::String);
    for(const Value& item:input) {
        if((!numbers&&!strings)||(numbers?!item.is_number():!item.is(Kind::String)))
            return raise(fault,StopReason::Error,"sort: incomparable values");
    }
    if(n>std::size_t(-1)/(2*sizeof(Value))||
       !natives::reserve_result(context,n*sizeof(Value)*(s[Source].refcount()>1?2:1)+128))
        return raise(fault,StopReason::Internal,"memory budget exceeded");
    s[HashTable]=make_array(n);
    for(std::size_t i=0;i<n;++i)array_push(s[HashTable],Value::integer(i));
    auto& order=array_mut(s[HashTable]);
    std::sort(order.begin(),order.end(),[&](const Value& x,const Value& y) {
        const auto a=static_cast<std::size_t>(x.as_int()),b=static_cast<std::size_t>(y.as_int());
        int c=0;
        if(numbers)c=natives::compare_numbers(input[a],input[b]);
        else {const auto l=string_view_of(input[a]),r=string_view_of(input[b]);c=l<r?-1:l>r?1:0;}
        return c?c<0:a<b;
    });
    auto& items=array_mut(s[Source]);
    for(std::size_t i=0;i<n;++i) {
        if(static_cast<std::size_t>(order[i].as_int())==i)continue;
        Value value=std::move(items[i]);std::size_t at=i;
        while(static_cast<std::size_t>(order[at].as_int())!=i) {
            const auto next=static_cast<std::size_t>(order[at].as_int());
            items[at]=std::move(items[next]);order[at]=Value::integer(at);at=next;
        }
        items[at]=std::move(value);order[at]=Value::integer(at);
    }
    s[Result]=s[Source];return true;
}
}
