// Test-only: the actual linked standard library over legacy RuntimeValue.
#include "RuntimeValue.h"
#include <algorithm>
#include <iostream>
#include <vector>
using nift::RuntimeValue;
static void json(const RuntimeValue& v) {
    if (v.is_number()) std::cout << static_cast<int>(v.num);
    else if (v.is_array()) { std::cout << '['; bool comma=false; for(const auto& x:v.array) {if(comma)std::cout<<',';comma=true;json(x);}std::cout<<']'; }
    else if(v.is_object()) {std::cout<<'{';bool comma=false;for(const auto& x:v.object){if(comma)std::cout<<',';comma=true;std::cout<<'"'<<x.first<<"\":";json(x.second);}std::cout<<'}';}
    else std::cout << "null";
}
static int key(const RuntimeValue& v) {
    if(v.is_number()) return static_cast<int>(v.num);
    for(const auto& x:v.object)if(x.first=="k")return static_cast<int>(x.second.num);
    std::abort();
}
int main() {
    int records,n,fail,desc,inner;
    while(std::cin>>records>>n>>fail>>desc>>inner) {
        RuntimeValue a=RuntimeValue::make_array();
        for(int i=0;i<n;++i){int x;std::cin>>x;RuntimeValue v(x);if(records){v=RuntimeValue::make_object();v.object.emplace_back("k",RuntimeValue(x));v.object.emplace_back("id",RuntimeValue(i));}a.array.push_back(std::move(v));}
        int calls=0;bool failed=false;
        std::cout<<"{\"trace\":[";
        std::stable_sort(a.array.begin(),a.array.end(),[&](const RuntimeValue& l,const RuntimeValue& r){
            if(failed)return false;
            if(calls)std::cout<<',';
            ++calls;std::cout<<"{\"l\":";json(l);std::cout<<",\"r\":";json(r);std::cout<<",\"a\":";json(a);std::cout<<'}';
            if(calls==fail&&!inner){failed=true;return false;}
            return desc?key(l)>key(r):key(l)<key(r);
        });
        std::cout<<"],\"final\":";json(a);std::cout<<",\"calls\":"<<calls<<",\"failed\":"<<(failed?"true":"false")<<"}\n";
    }
}
