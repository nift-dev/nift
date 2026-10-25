// Fail real ABI allocations: exceptions stay contained and VM-owned heap drains.
#include "nift/c_abi.h"
#include "rt/heap_account.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <cstring>

static long remaining = -1;
static bool failed = false;
void* operator new(std::size_t bytes) {
    if (remaining == 0) { remaining = -1; failed = true; throw std::bad_alloc(); }
    if (remaining > 0) --remaining;
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); } } while (0)
static std::size_t live() { return nift::rt::live_heap_bytes() - nift::rt::heap_charged_bytes(); }
int main() {
    const char* text = "a := [1,2,3]; fn(f(x)) { return x + 1; } print(f(a[0])); throw error(\"owned\");";
    nift_source source = {NIFT_SOURCE_TEXT,text,std::strlen(text),"oom.f",5};
    int compile_sites = 0, run_sites = 0;
    bool compile_done = false, run_done = false;
    // Warm interned names before taking the owned-heap baseline.
    nift_program* program = nullptr; nift_program_result* result = nullptr;
    CHECK(nift_program_compile(&source,&program,&result) == NIFT_OK && program);
    nift_program_result_free(result);
    CHECK(nift_program_run(program,0,0,0,&result) == NIFT_OK);
    nift_program_result_free(result); nift_program_free(program);
    const auto before = live();
    for (long i = 0; i < 5000; ++i) {
        program = nullptr; result = nullptr; failed = false; remaining = i;
        const auto status = nift_program_compile(&source,&program,&result);
        remaining = -1;
        if (failed) { ++compile_sites; CHECK(status == NIFT_ERROR_INTERNAL); CHECK(!program && !result); }
        else CHECK(status == NIFT_OK && program && result);
        nift_program_result_free(result); nift_program_free(program);
        CHECK(live() == before);
        if (!failed) { compile_done = true; break; }
    }
    CHECK(nift_program_compile(&source,&program,&result) == NIFT_OK && program);
    nift_program_result_free(result);
    const auto compiled_live = live();
    for (long i = 0; i < 5000; ++i) {
        result = nullptr; failed = false; remaining = i;
        const auto status = nift_program_run(program,0,0,0,&result);
        remaining = -1;
        if (failed) { ++run_sites; CHECK(status == NIFT_ERROR_INTERNAL); CHECK(!result); }
        else CHECK(status == NIFT_OK && result && nift_program_result_outcome(result) == NIFT_PROGRAM_ERROR);
        nift_program_result_free(result);
        CHECK(live() == compiled_live);
        if (!failed) { run_done = true; break; }
    }
    // An allocation failure must not poison the reusable program.
    CHECK(nift_program_run(program,0,0,0,&result) == NIFT_OK);
    CHECK(nift_program_result_outcome(result) == NIFT_PROGRAM_ERROR);
    nift_program_result_free(result); nift_program_free(program); CHECK(live() == before);
    CHECK(compile_done && run_done && compile_sites > 20 && run_sites > 5);
    std::printf("CP18 allocation containment: %d compile sites, %d run sites; %d failures\n",
                compile_sites,run_sites,failures);
    return failures ? 1 : 0;
}
