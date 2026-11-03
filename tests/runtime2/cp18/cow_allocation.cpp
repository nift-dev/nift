// Fail real ABI allocations: exceptions stay contained and VM-owned heap drains.
#include "nift/c_abi.h"
#include "rt/heap_account.h"
#include "rt/aggregate.h"
#include "rt/copy_graph.h"
#include "rt/mutation.h"
#include "rt/cell.h"
#include "rt/compare.h"
#include "rt/render.h"
#include "rt/session.h"
#include <functional>
#include <string>
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
    // B2 (b2-refrep.md): was `fn(g(a)) { a.push(1); ... } b := [5]; g(b)`, the
    // copy-on-write clone of a shared argument through the program API; the
    // clone is deleted and the Phase A guard refuses that program
    // (reference-guard:argument, already before B2). The same output from an
    // array grown through the mutation protocol inside a call.
    const char* text = "fn(g(n)) { a := [n]; a.push(1); return a.length(); } b := [5]; print(g(5)); print(b.length());";
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
// B2 (b2-refrep.md): the explicit clone sites that replaced unshare
// (array_shallow_clone / object_shallow_clone: loop and callback snapshots,
// splice, cell descriptors; the struct shallow copy) fail at every allocation in turn: the original is unchanged, nothing
// leaks (live heap and outstanding allocations equal), and the completed
// clone is independent of the original. Replaces the unshare campaign (its
// subject, the copy-on-write clone, is deleted).
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
            const auto unique_live = live();
            const auto unique_allocations = outstanding_allocations;
            Value clone;
            failed = false; remaining = i;
            try {
                if (kind == 0) clone = array_shallow_clone(original);
                else if (kind == 1) clone = object_shallow_clone(original);
                else CHECK(shallow_copy(original,clone,kNoCopyLimit) == CopyStatus::Ok);
            }
            catch (const std::bad_alloc&) { CHECK(failed); }
            remaining = -1;
            const bool injected = failed;
            if (injected) {
                ++sites;
                CHECK(clone.is_null() && original.heap() == identity && original.refcount() == 1);
                CHECK(live() == unique_live);
                CHECK(outstanding_allocations == unique_allocations);
                if (outstanding_allocations != unique_allocations)
                    std::fprintf(stderr,"  clone kind %d: failure at allocation %ld leaves %ld allocations\n",kind,i,
                                 outstanding_allocations - unique_allocations);
                check_contents(kind,original,1);
                if (kind == 0) clone = array_shallow_clone(original);
                else if (kind == 1) clone = object_shallow_clone(original);
                else CHECK(shallow_copy(original,clone,kNoCopyLimit) == CopyStatus::Ok);
            } else completed = true;
            CHECK(original.heap() == identity && clone.heap() != identity);
            check_contents(kind,original,1);
            check_contents(kind,clone,1);
            const mutation::Env env;
            if (kind == 0) CHECK(mutation::set(clone,1,Value::integer(999),env).ok());
            else if (kind == 1) CHECK(mutation::object_put(clone,keys[1],Value::integer(999),env).ok());
            else CHECK(mutation::field_set(clone,keys[1],Value::integer(999),env).ok());
            check_contents(kind,original,1);
            check_contents(kind,clone,999);
        }
        CHECK(live() == empty_live);
        CHECK(outstanding_allocations == empty_allocations);
        if (completed) break;
    }
    CHECK(completed && sites >= 2);
    std::printf("clone direct %s: %d allocation sites; complete %d\n",
                kind == 0 ? "array" : kind == 1 ? "object" : "struct",sites,completed);
}

// B2: the protocol's growth and reservation paths (push past capacity,
// insert, splice, map/set/stack insertion) fail at every allocation in turn:
// the receiver is unchanged (size and contents) and nothing leaks.
static void growth_campaign(int which) {
    using namespace nift::rt;
    int sites = 0;
    bool completed = false;
    const char* names[] = {"push","insert","splice","map","set","stack","splice-self"};
    for (long i = 0; i < 100 && !completed; ++i) {
        const auto empty_live = live();
        const auto empty_allocations = outstanding_allocations;
        {
            Value r = (which < 3 || which == 6) ? make_array(4) : make_collection(which == 3 ? CollectionKind::Map :
                                                                  which == 4 ? CollectionKind::Set : CollectionKind::Stack);
            for (int k = 0; k < 4; ++k) {
                if (which < 3 || which == 6) array_push(r,make_string("an element long enough for the heap"));
                else if (which == 3) map_build_set(r,Value::integer(k),make_string("a value long enough for the heap"));
                else if (which == 4) set_build_add(r,Value::integer(k));
                else stack_build_push(r,Value::integer(k));
            }
            const bool array = which < 3 || which == 6;
            const std::size_t size = array ? array_size(r) : collection_item_count(r);
            Value extra = make_array(2);
            array_push(extra,Value::integer(7));
            array_push(extra,make_string("an inserted element long enough for the heap"));
            const auto before_live = live();
            const auto before_allocations = outstanding_allocations;
            const mutation::Env env;
            failed = false; remaining = i;
            bool threw = false;
            try {
                mutation::Outcome out;
                switch (which) {
                    case 0: out = mutation::push(r,make_string("a pushed element long enough for the heap"),env); break;
                    case 1: out = mutation::insert(r,1,extra,env); break;
                    case 2: out = mutation::splice(r,1,1,extra,nullptr,env); break;
                    case 6: out = mutation::splice(r,1,1,r,nullptr,env); break;  // the self copy is prepared first
                    case 3: out = mutation::map_put(r,Value::integer(100),make_string("value"),env); break;
                    case 4: out = mutation::set_insert(r,Value::integer(100),env); break;
                    default: out = mutation::stack_push(r,Value::integer(100),env); break;
                }
                CHECK(out.ok());
            } catch (const std::bad_alloc&) { threw = true; CHECK(failed); }
            remaining = -1;
            if (threw) {
                ++sites;
                CHECK((array ? array_size(r) : collection_item_count(r)) == size);
                CHECK(live() == before_live);
                CHECK(outstanding_allocations <= before_allocations);
            } else completed = true;
        }
        CHECK(live() == empty_live);
        CHECK(outstanding_allocations == empty_allocations);
    }
    CHECK(completed && sites >= 1);
    std::printf("growth %s: %d allocation sites; complete %d\n",names[which],sites,completed);
}
// B2 (owner, 1b item 2): the reference model's allocation-failure contract,
// each campaign failing allocation k = 0, 1, ... until the step completes:
// after a failure the runtime is valid, nothing leaks (live heap and
// outstanding allocations back to the baseline once the step's values are
// gone) and nothing reachable is destroyed early (the source graph is intact).
static void campaign(const char* name, const std::function<void()>& step, const std::function<bool()>& intact_check) {
    using namespace nift::rt;
    int sites = 0;
    bool completed = false;
    for (long i = 0; i < 400 && !completed; ++i) {
        const auto base_live = live();
        const auto base_allocations = outstanding_allocations;
        failed = false; remaining = i;
        try { step(); } catch (const std::bad_alloc&) { CHECK(failed); }
        remaining = -1;
        if (failed) ++sites; else completed = true;
        CHECK(intact_check());
        CHECK(live() == base_live);
        CHECK(outstanding_allocations == base_allocations);
        if (live() != base_live || outstanding_allocations != base_allocations)
            std::fprintf(stderr,"  %s: failure at allocation %ld leaks\n",name,i);
    }
    CHECK(completed && sites >= 1);
    std::printf("reference model %s: %d allocation sites; complete %d\n",name,sites,completed);
}

// A graph with shared and nested references (an array held twice, an object
// holding it, a struct, a map, a closure capturing a cell): what copies and
// growth meet in the reference model. Built outside the injection.
static nift::rt::Value shared_graph() {
    using namespace nift::rt;
    Value leaf = make_array();
    array_push(leaf,make_string("a shared leaf long enough for the heap"));
    Value obj = make_object();
    object_set(obj,keys[0],leaf);
    Value st = make_struct(type_name);
    struct_add_field(st,keys[1],obj);
    Value map = make_collection(CollectionKind::Map);
    map_build_set(map,Value::integer(1),leaf);
    auto* f = new FunctionObject();
    Value closure = Value::adopt(f);
    f->upvalues.push_back(make_cell(leaf,-1));
    Value root = make_array();
    for (const Value& v : {leaf,leaf,obj,st,map,closure}) array_push(root,v);
    return root;
}
static std::string shown(const nift::rt::Value& v) {
    std::string out;
    nift::rt::append_rendered(v,out);
    return out;
}

static void reference_model_campaigns() {
    using namespace nift::rt;
    // Creation of every shared-reference object (arrays, objects, instances,
    // collections, cells: the representation's allocations on creation).
    campaign("create",[] {
        Value a = make_array(8), o = make_object(), s = make_struct(type_name,8);
        Value c = make_collection(CollectionKind::Set), cell = make_cell(a,-1);
    },[] { return true; });
    // copy() and deepcopy() of a graph with shared and nested references: a
    // partial copy never leaks and the source is unchanged (value and identity).
    Value source = shared_graph();
    const std::string before = shown(source);
    const HeapObject* leaf = array_items(source)[0].heap();
    const auto source_intact = [&] {
        return shown(source) == before && array_items(source)[0].heap() == leaf &&
               array_items(source)[1].heap() == leaf;
    };
    Value out;
    campaign("copy",[&] { out = Value(); CHECK(shallow_copy(source,out,kNoCopyLimit) == CopyStatus::Ok); },
             [&] { const bool ok = source_intact() && (out.is_null() || array_items(out)[0].heap() == leaf);
                   out = Value(); return ok; });
    campaign("deepcopy",[&] { CHECK(deep_copy(source,out,kNoCopyLimit) == CopyStatus::Ok); },
             [&] { const bool ok = source_intact() && (out.is_null() || (shown(out) == before &&
                       array_items(out)[0].heap() == array_items(out)[1].heap() && array_items(out)[0].heap() != leaf));
                   out = Value(); return ok; });
    // Growth with nested references present: the receiver holds the shared
    // graph, the stored value holds references; a refused cycle allocates nothing.
    Value r;
    campaign("nested-growth",[&] {
        r = make_array(1);
        array_push(r,source);
        const mutation::Env env;
        CHECK(mutation::push(r,source,env).ok());
        CHECK(mutation::push(r,r,env).failure == mutation::Failure::Cycle);
    },[&] { const bool ok = source_intact() && (r.is_null() || array_size(r) <= 2); r = Value(); return ok; });
}

// A persistent session under repeated allocation failure: a failed statement
// never corrupts or nulls a binding, later statements recover, and nothing
// leaks across statements or after the session.
static void session_campaign() {
    using namespace nift::rt;
    const auto base_allocations = outstanding_allocations;
    {
        VmSession session;
        std::string out;
        CHECK(session.execute("a := [1, [2]]\nb := 0","seed",out).execution.reason == StopReason::None);
        int sites = 0, recovered = 0;
        bool completed = false, closed = false;
        for (long i = 0; i < 20000; ++i) {
            failed = false; remaining = i;
            SessionResult r;
            bool threw = false;
            try { r = session.execute("a.push([b, [b]])\nb += 1","grow",out); }
            catch (const std::bad_alloc&) { CHECK(failed); threw = true; }
            remaining = -1;
            if (!failed) { completed = true; break; }
            ++sites;
            const Value now = session.value("a");
            if (!now.is(Kind::Array) && r.execution.reason == StopReason::Internal) {
                // CP19 design (session.cpp): an internal stop (here bad_alloc
                // inside the VM) closes the session; its bindings are dropped
                // deliberately, not corrupted. Nothing may leak (checked below).
                closed = true;
                break;
            }
            const Value a = session.value("a");
            CHECK(a.is(Kind::Array) && array_size(a) >= 2);  // valid, never null
            if (!a.is(Kind::Array))
                std::fprintf(stderr,"  session: failure at allocation %ld (threw %d, reason %d '%s')\n",
                             i,threw,int(r.execution.reason),r.execution.message.c_str());
            out.clear();
            if (session.execute("print(a.length())","recover",out).execution.reason == StopReason::None) ++recovered;
        }
        CHECK((completed || closed) && sites >= 1 && recovered == sites - (closed ? 1 : 0));
        CHECK(sites >= 100);  // the compile and early execution phases recover
        out.clear();
        if (completed)
            CHECK(session.execute("print(a.length())","final",out).execution.reason == StopReason::None);
        std::printf("reference model session: %d allocation sites; %d recoveries; complete %d; closed by an "
                    "internal stop %d\n",sites,recovered,completed,closed);
    }
    CHECK(outstanding_allocations == base_allocations);
}

int main() {
    using namespace nift::rt;
    for (int i = 0; i < 10; ++i) { char key[32]; std::snprintf(key,sizeof key,"cow-key-%d",i); keys[i] = atom(key); }
    type_name = atom("CowDirectStruct");
    abi_campaign();
    for (int kind = 0; kind < 3; ++kind) direct_campaign(kind);
    for (int which = 0; which < 7; ++which) growth_campaign(which);
    reference_model_campaigns();
    session_campaign();
    std::printf("COW final: %d failures\n",failures);
    std::fflush(stdout);
    return failures ? 1 : 0;
}
