#pragma once
// Index-only compatibility-sort controller with frame-owned scratch.
#include "rt/collection.h"
namespace nift::rt {
namespace sort_slot {
inline constexpr unsigned Left=12,Right=13,EmptyObject=14,EmptyArray=15,EmptyString=16,KeepAlive=17,Count=18;
}
// All owning state is in the activation's hidden Value registers. The POD
// controller in HashTable contains only indices and phases, never pointers.
bool collection_sort_init(Value*,const natives::Context&,Fault&,std::size_t site);
bool collection_sort_active(const Value*,std::size_t site);
bool collection_sort_resume(Value*,bool answer,bool draining);
}
