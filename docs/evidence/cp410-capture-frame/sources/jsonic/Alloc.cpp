#include "Phases.h"
#include <cstddef>
#include <cstdint>
struct alignas(std::max_align_t) Header {void* raw;std::size_t size;};
void* get(std::size_t n,std::size_t a){const auto size=n?n:1;void* raw=std::malloc(size+sizeof(Header)+a);if(!raw)throw std::bad_alloc();auto u=(reinterpret_cast<std::uintptr_t>(raw)+sizeof(Header)+a-1)&~(a-1);auto* h=reinterpret_cast<Header*>(u)-1;h->raw=raw;h->size=n;cp51::allocated(n);return reinterpret_cast<void*>(u);}
void release(void* p){if(!p)return;auto* h=static_cast<Header*>(p)-1;cp51::freed(h->size);std::free(h->raw);}
void* operator new(std::size_t n){return get(n,alignof(std::max_align_t));}void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{release(p);}void operator delete[](void*p)noexcept{release(p);}void operator delete(void*p,std::size_t)noexcept{release(p);}void operator delete[](void*p,std::size_t)noexcept{release(p);}
void* operator new(std::size_t n,std::align_val_t a){return get(n,static_cast<std::size_t>(a));}void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{release(p);}void operator delete[](void*p,std::align_val_t)noexcept{release(p);}void operator delete(void*p,std::size_t,std::align_val_t)noexcept{release(p);}void operator delete[](void*p,std::size_t,std::align_val_t)noexcept{release(p);}
