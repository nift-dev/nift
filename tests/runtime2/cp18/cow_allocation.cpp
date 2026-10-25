// Fail real ABI allocations: exceptions stay contained and VM-owned heap drains.
#include "nift/c_abi.h"
#include "rt/heap_account.h"
#include "rt/aggregate.h"
#include "rt/string.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <cstring>

static long remaining = -1;
static bool failed = false;
static long outstanding_allocations = 0;
void* operator new(std::size_t bytes) {
    if (remaining == 0) { remaining = -1; failed = true; throw std::bad_alloc(); }
    if (remaining > 0) --remaining;
    if (void* p = std::malloc(bytes ? bytes : 1)) { ++outstanding_allocations; return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { if (p) --outstanding_allocations; std::free(p); }
void operator delete[](void* p) noexcept { if (p) --outstanding_allocations; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (p) --outstanding_allocations; std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (p) --outstanding_allocations; std::free(p); }
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); } } while (0)
static std::size_t live() { return nift::rt::live_heap_bytes() - nift::rt::heap_charged_bytes(); }
void abi_campaign() {
    const char* text = "fn(g(a)) { a.push(1); return a.length(); } b := [5]; print(g(b)); print(b.length());";
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
    const auto before_allocations = outstanding_allocations;
    for (long i = 0; i < 5000; ++i) {
        program = nullptr; result = nullptr; failed = false; remaining = i;
        const auto status = nift_program_compile(&source,&program,&result);
        remaining = -1;
        if (failed) { ++compile_sites; CHECK(status == NIFT_ERROR_INTERNAL); CHECK(!program && !result); }
        else CHECK(status == NIFT_OK && program && result);
        nift_program_result_free(result); nift_program_free(program);
        CHECK(live() == before);
        CHECK(outstanding_allocations == before_allocations);
        if (!failed) { compile_done = true; break; }
    }
    CHECK(nift_program_compile(&source,&program,&result) == NIFT_OK && program);
    nift_program_result_free(result);
    const auto compiled_live = live();
    const auto compiled_allocations = outstanding_allocations;
    for (long i = 0; i < 5000; ++i) {
        result = nullptr; failed = false; remaining = i;
        const auto status = nift_program_run(program,0,0,0,&result);
        remaining = -1;
        if (failed) { ++run_sites; CHECK(status == NIFT_ERROR_INTERNAL); CHECK(!result); }
        else CHECK(status == NIFT_OK && result && nift_program_result_outcome(result) == NIFT_PROGRAM_COMPLETE);
        nift_program_result_free(result);
        CHECK(live() == compiled_live);
        CHECK(outstanding_allocations == compiled_allocations);
        const bool injected = failed;
        if (injected) {
            CHECK(nift_program_run(program,0,0,0,&result) == NIFT_OK);
            CHECK(nift_program_result_outcome(result) == NIFT_PROGRAM_COMPLETE);
            nift_string output{};
            CHECK(nift_program_result_output(result,&output) == NIFT_OK);
            CHECK(output.length == 4 && std::memcmp(output.data,"2\n1\n",4) == 0);
            nift_program_result_free(result);
            CHECK(live() == compiled_live);
            CHECK(outstanding_allocations == compiled_allocations);
        }
        if (!injected) { run_done = true; break; }
    }
    // An allocation failure must not poison the reusable program.
    CHECK(nift_program_run(program,0,0,0,&result) == NIFT_OK);
    CHECK(nift_program_result_outcome(result) == NIFT_PROGRAM_COMPLETE);
    nift_program_result_free(result); nift_program_free(program); CHECK(live() == before);
    CHECK(compile_done && run_done && compile_sites > 20 && run_sites > 5);
    std::printf("CP18 allocation containment: %d compile sites, %d run sites; %d failures\n",
                compile_sites,run_sites,failures);
    std::printf("Outstanding allocations after program destruction delta %ld\n", outstanding_allocations-before_allocations);
    CHECK(outstanding_allocations == before_allocations);
}


static nift::rt::Atom keys[10], type_name;
static nift::rt::Value fixture(int kind) {
    using namespace nift::rt;
    Value value = kind == 0 ? make_array() : kind == 1 ? make_object() : make_struct(type_name);
    for (int i = 0; i < 10; ++i) {
        Value child = i == 0 ? make_string("retained non-inline child for clone cleanup") :
            i == 9 ? Value::handle(HandleKind::Custom,1,1) : Value::integer(i);
        if (kind == 0) array_push(value,static_cast<Value&&>(child));
        else if (kind == 1) object_set(value,keys[i],static_cast<Value&&>(child));
        else struct_add_field(value,keys[i],static_cast<Value&&>(child));
    }
    return value;
}
static void check_contents(int kind, const nift::rt::Value& value, long expected) {
    using namespace nift::rt;
    const Value* child = kind == 0 ? &array_items(value)[1] :
        kind == 1 ? object_get(value,keys[1]) : struct_get(value,keys[1]);
    CHECK(child && child->is(Kind::Int) && child->as_int() == expected);
    CHECK(may_contain_identity(value));
    if (kind == 2) CHECK(struct_type(value) == type_name);
}
static void direct_campaign(int kind) {
    using namespace nift::rt;
    int sites = 0;
    bool completed = false;
    for (long i = 0; i < 100; ++i) {
        const auto empty_live = live();
        const auto empty_allocations = outstanding_allocations;
        {
            Value original = fixture(kind);
            const auto* identity = original.heap();
            const auto unique_allocations = outstanding_allocations;
            CHECK(!unshare(original));
            CHECK(original.heap() == identity && outstanding_allocations == unique_allocations);
            Value clone = original;
            const auto shared_live = live();
            const auto shared_allocations = outstanding_allocations;
            failed = false; remaining = i;
            try { CHECK(unshare(clone)); }
            catch (const std::bad_alloc&) { CHECK(failed); }
            remaining = -1;
            const bool injected = failed;
            if (injected) {
                ++sites;
                CHECK(clone.heap() == original.heap());
                CHECK(live() == shared_live);
                CHECK(outstanding_allocations == shared_allocations);
                check_contents(kind,original,1);
                check_contents(kind,clone,1);
                CHECK(unshare(clone));
            } else completed = true;
            CHECK(original.heap() == identity && clone.heap() != identity);
            check_contents(kind,original,1);
            check_contents(kind,clone,1);
            if (kind == 0) array_set(clone,1,Value::integer(999));
            else if (kind == 1) object_set(clone,keys[1],Value::integer(999));
            else CHECK(struct_set(clone,keys[1],Value::integer(999)));
            check_contents(kind,original,1);
            check_contents(kind,clone,999);
        }
        CHECK(live() == empty_live);
        CHECK(outstanding_allocations == empty_allocations);
        if (completed) break;
    }
    CHECK(completed && sites >= 2);
    std::printf("COW direct %s: %d allocation sites; complete %d\n",
                kind == 0 ? "array" : kind == 1 ? "object" : "struct",sites,completed);
}
int main() {
    using namespace nift::rt;
    for (int i = 0; i < 10; ++i) { char key[32]; std::snprintf(key,sizeof key,"cow-key-%d",i); keys[i] = atom(key); }
    type_name = atom("CowDirectStruct");
    abi_campaign();
    for (int kind = 0; kind < 3; ++kind) direct_campaign(kind);
    std::printf("COW final: %d failures\n",failures);
    std::fflush(stdout);
    return failures ? 1 : 0;
}
