// B3 (docs/runtime-2/checkpoints/b3-structs.md): struct type descriptors and
// privacy-aware struct rendering, at the value layer.
//   descriptor   creation, versions, content interning, one table per isolate
//   forms        legacy's byte-exact compact and pretty forms (D1, D6)
//   transfer     the descriptor crosses by value and is interned (D2)
//   copies       copy/deepcopy keep the instance's descriptor
//   cycles       a C++-built cyclic struct graph stops with the depth error
//   observe      rendering changes no refcount and no heap byte
//   pairing      field i pairs with descriptor field i; the fallback pairs by
//                name and never renders an undeclared field (fail closed, D7);
//                checked builds abort on a mispaired construction
#include "rt/aggregate.h"
#include "rt/aggregate_store.h"  // NIFT_RT_RELOCATE_ON_MUTATION: the checked-build pairing abort
#include "rt/atom.h"
#include "rt/copy_graph.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/isolate_transfer.h"
#include "rt/natives/format_methods.h"
#include "rt/string.h"
#include "rt/struct_type.h"
#include "storage_access.h"  // the construction shims (tests only)
#include "rt_test.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace nift::rt;

namespace {

Value I(std::int64_t i) { return Value::integer(i); }
Value S(const char* s) { return make_string(s); }
const char* kSecret = "SECRET-b3-sentinel";

std::string render(const Value& v, bool pretty = false) {
    std::string out, error;
    bool internal = false;
    if (natives::serialize(v, pretty, out, error, internal)) return out;
    return "ERR:" + error + (internal ? " (internal)" : "");
}

bool hidden(const std::string& text) { return text.find(kSecret) == std::string::npos; }

struct NoProtos : TransferProtoMap {
    bool index_of(const Proto*, std::uint32_t&) const override { return false; }
    const Proto* proto_of(std::uint32_t) const override { return nullptr; }
};

std::shared_ptr<const StructType> type_c() { return StructType::make("C", {"a", "b", "c"}, {false, true, false}); }

Value make_c(const std::shared_ptr<const StructType>& type) {
    Value s = make_struct(type, 3);
    struct_add_field(s, atom("a"), I(1));
    struct_add_field(s, atom("b"), S(kSecret));
    Value list = make_array();
    array_push(list, I(1));
    array_push(list, I(2));
    struct_add_field(s, atom("c"), list);
    return s;
}

void descriptor() {
    const auto a = StructType::make("P", {"x", "y"}, {true});
    const auto b = StructType::make("P", {"x", "y"}, {true});
    CHECK(a->version != b->version && a->private_fields.size() == 2 && a->is_private(0) && !a->is_private(1));
    CHECK(!a->is_private(7) && a->position("y") == 1 && a->position("z") == -1);
    CHECK(!a->same_content(*b));  // another schema version
    StructTypeTable& table = StructTypeTable::current();
    const std::size_t before = table.size();
    const auto ia = table.intern(*a), ia2 = table.intern(*a), ib = table.intern(*b);
    CHECK(ia == ia2 && ia != ib && ia != a && ia->same_content(*a) && table.size() == before + 2);
    StructType open = *a;
    open.private_fields.assign(2, false);
    CHECK(table.intern(open) != ia);  // privacy is part of the content
    {
        AtomTableScope isolate;  // another isolate: its own, empty table
        CHECK(StructTypeTable::current().size() == 0 && &StructTypeTable::current() != &table);
        CHECK(StructTypeTable::current().intern(*a) != ia);
    }
    CHECK(&StructTypeTable::current() == &table && table.size() == before + 3);
}

void forms() {
    const Value c = make_c(type_c());
    CHECK(render(c) == "C{a:1,c:[1,2]}");
    CHECK(render(c, true) == "C{\n  a: 1,\n  c: [\n    1,\n    2\n  ],\n}");
    const auto d = StructType::make("D", {"x"}, {true});
    Value dv = make_struct(d, 1);
    struct_add_field(dv, atom("x"), S(kSecret));
    CHECK(render(dv) == "D{}" && render(dv, true) == "D{}");
    const Value e = make_struct(StructType::make("E", {}, {}));
    CHECK(render(e) == "E{}" && render(e, true) == "E{}");
    Value list = make_array();
    array_push(list, c);
    array_push(list, c);  // an alias renders at each occurrence (legacy)
    CHECK(render(list) == "[C{a:1,c:[1,2]},C{a:1,c:[1,2]}]");
    CHECK(render(list, true) == "[\n  C{\n    a: 1,\n    c: [\n      1,\n      2\n    ],\n  },\n  C{\n    a: 1,\n"
                                "    c: [\n      1,\n      2\n    ],\n  }\n]");
    Value outer = make_struct(StructType::make("Out", {"i", "p"}, {false, true}), 2);
    struct_add_field(outer, atom("i"), c);
    struct_add_field(outer, atom("p"), c);
    CHECK(render(outer) == "Out{i:C{a:1,c:[1,2]}}");
    CHECK(render(outer, true) == "Out{\n  i: C{\n    a: 1,\n    c: [\n      1,\n      2\n    ],\n  },\n}");
    Value o = make_object();
    object_set(o, atom("x"), c);
    CHECK(render(o) == "{\"x\":C{a:1,c:[1,2]}}");
    Value m = make_collection(CollectionKind::Map);
    map_build_set(m, S("k"), c);
    CHECK(render(m) == "map([[\"k\",C{a:1,c:[1,2]}]])");
    Value st = make_collection(CollectionKind::Stack);
    stack_build_push(st, c);
    CHECK(render(st) == "stack([C{a:1,c:[1,2]}])");
    for (const Value* v : std::initializer_list<const Value*>{&c, &list, &outer, &o, &m, &st})
        CHECK(hidden(render(*v)) && hidden(render(*v, true)));
}

void transfer() {
    NoProtos protos;
    const auto type = type_c();
    Value list = make_array();
    array_push(list, make_c(type));
    array_push(list, make_c(type));
    TransferGraph graph;
    std::string error;
    CHECK(graph.encode(list, protos, error) == TransferStatus::Ok);
    Value back;
    CHECK(graph.decode(protos, back, error) == TransferStatus::Ok);
    const Value& first = array_items(back)[0];
    const Value& second = array_items(back)[1];
    CHECK(struct_type_ref(first) != type && struct_type(first).same_content(*type));  // by value, interned
    CHECK(struct_type_ref(first) == struct_type_ref(second));  // one descriptor per content
    CHECK(struct_type(first).is_private(1) && render(back) == render(list) && hidden(render(back, true)));
    Value again;
    CHECK(graph.decode(protos, again, error) == TransferStatus::Ok);
    CHECK(struct_type_ref(array_items(again)[0]) == struct_type_ref(first));  // the same isolate's table
    {
        AtomTableScope isolate;  // decoded into another isolate: its own descriptor, privacy kept
        Value there;
        CHECK(graph.decode(protos, there, error) == TransferStatus::Ok);
        const Value& t = array_items(there)[0];
        CHECK(struct_type_ref(t) != struct_type_ref(first) && struct_type(t).same_content(*type));
        CHECK(render(t) == "C{a:1,c:[1,2]}" && hidden(render(there, true)));
        TransferGraph home;  // and back again
        CHECK(home.encode(there, protos, error) == TransferStatus::Ok);
        there = Value();
        Value returned;
        CHECK(home.decode(protos, returned, error) == TransferStatus::Ok && hidden(render(returned)));
    }
}

void copies() {
    const Value c = make_c(type_c());
    Value shallow, deep;
    CHECK(shallow_copy(c, shallow, std::numeric_limits<std::size_t>::max()) == CopyStatus::Ok);
    CHECK(deep_copy(c, deep, std::numeric_limits<std::size_t>::max()) == CopyStatus::Ok);
    CHECK(shallow.heap() != c.heap() && deep.heap() != c.heap());
    CHECK(struct_type_ref(shallow) == struct_type_ref(c) && struct_type_ref(deep) == struct_type_ref(c));
    CHECK(render(shallow) == "C{a:1,c:[1,2]}" && render(deep, true) == render(c, true) && hidden(render(deep)));
}

void cycles() {
    const auto n = StructType::make("N", {"next", "s"}, {false, true});
    Value a = make_struct(n, 2), b = make_struct(n, 2);
    struct_add_field(a, atom("next"), Value());
    struct_add_field(a, atom("s"), S(kSecret));
    struct_add_field(b, atom("next"), Value());
    struct_add_field(b, atom("s"), S(kSecret));
    CHECK(struct_set(a, atom("next"), b) && struct_set(b, atom("next"), a));  // a cycle, test shim only
    const std::string text = render(a);
    CHECK(text == "ERR:value serialization depth exceeded");
    CHECK(render(a, true) == "ERR:value serialization depth exceeded");
    CHECK(struct_set(b, atom("next"), Value()));  // break it (the values can then be freed)
    CHECK(render(a) == "N{next:N{next:null}}");
    // A chain of 128 instances renders (its last null at depth 128); 129 do
    // not (legacy's limit: depth > 128 fails).
    Value chain;
    for (int k = 0; k < 128; ++k) {
        Value link = make_struct(n, 2);
        struct_add_field(link, atom("next"), chain);
        struct_add_field(link, atom("s"), S(kSecret));
        chain = link;
    }
    CHECK(render(chain).rfind("N{next:", 0) == 0 && hidden(render(chain)));
    Value deeper = make_struct(n, 2);
    struct_add_field(deeper, atom("next"), chain);
    struct_add_field(deeper, atom("s"), S(kSecret));
    CHECK(render(deeper) == "ERR:value serialization depth exceeded");
}

void observe() {
    const auto type = type_c();
    const Value c = make_c(type);
    Value list = make_array();
    array_push(list, c);
    const auto refs = c.refcount();
    const auto uses = type.use_count();
    const Value* field = struct_get(c, atom("c"));
    const auto field_refs = field->refcount();
    const std::size_t live = live_heap_bytes();
    std::string first = render(list, true);
    first.clear();
    first.shrink_to_fit();
    CHECK(c.refcount() == refs && type.use_count() == uses && field->refcount() == field_refs);
    CHECK(live_heap_bytes() == live && struct_fields(c).size() == 3);
    CHECK(string_view_of(*struct_get(c, atom("b"))) == kSecret);  // unchanged, still there
}

void pairing() {
    const auto type = type_c();
    const Value c = make_c(type);
    for (std::size_t i = 0; i < struct_fields(c).size(); ++i)
        CHECK(atom_text(struct_fields(c)[i].first) == type->fields[i]);
#if !defined(NIFT_RT_RELOCATE_ON_MUTATION)
    // Release builds: a mispaired instance (fields out of order, an undeclared
    // field, a declared one missing) renders by name and never shows a field
    // its descriptor does not declare, nor a private one.
    const auto t = StructType::make("T", {"a", "b", "c"}, {false, true, false});
    Value s = make_struct(t, 3);
    struct_add_field(s, atom("b"), S(kSecret));
    struct_add_field(s, atom("z"), S("UNDECLARED"));
    struct_add_field(s, atom("a"), I(5));
    CHECK(render(s) == "T{a:5}");
    CHECK(render(s, true) == "T{\n  a: 5,\n}");
    CHECK(hidden(render(s)) && render(s).find("UNDECLARED") == std::string::npos);
    Value extra = make_c(type);
    struct_add_field(extra, atom("z"), S("UNDECLARED"));  // beyond the declared fields
    CHECK(render(extra) == "C{a:1,c:[1,2]}");
#else
    // Checked builds: constructing a mispaired instance aborts.
#if defined(__unix__) || defined(__APPLE__)
    std::fflush(nullptr);
    const pid_t child = fork();
    if (child == 0) {
        const auto t = StructType::make("T", {"a", "b"}, {false, true});
        Value s = make_struct(t, 2);
        struct_add_field(s, atom("b"), I(1));  // `a` belongs at position 0
        _exit(0);
    }
    int status = 0;
    CHECK(child > 0 && waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#endif
#endif
}

} // namespace

int main() {
    descriptor();
    forms();
    transfer();
    copies();
    cycles();
    observe();
    pairing();
    return rt_test::finish("struct-render-unit");
}
