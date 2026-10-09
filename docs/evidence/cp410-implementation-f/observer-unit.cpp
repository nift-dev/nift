#include "RuntimeValue.h"
#include <cassert>
#include <iostream>
int main(){nift::RuntimeValue a=nift::RuntimeValue::make_object();a["x"]=1;a["y"]=2;assert(a.has("x"));assert(static_cast<const nift::RuntimeValue&>(a)["y"].num==2);assert(!a.find_member("missing"));auto b=a;b["x"]=4;for(const auto& entry:nift::workload_object_range(a))assert(entry.second.is_number());std::cout<<"OK\n";}
