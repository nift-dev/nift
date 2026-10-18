// CP14 built-in natives: unit tests (results and legacy messages for every
// native, table invariants, receiver immutability and output aliasing,
// exact heap accounting, the CP09 reference contract under relocation).
#include "builtins_support.h"
#include "rt_test.h"

#include "rt/compare.h"
#include "rt/decimal.h"
#include "rt/natives/object_methods.h"
#include "rt/natives/support.h"

#include <cmath>
#include <initializer_list>
#include <set>

using namespace nift::rt;
using namespace nift::rt::natives;
using builtins_test::shown;

namespace {

struct Result {
    Status status;
    Value out;
    std::string error;
};

Result call(BuiltinId id, std::initializer_list<Value> list, std::size_t heap_limit = 0) {
    std::vector<Value> args(list);
    Result r{Status::Ok, Value(), {}};
    Context context;
    context.heap_limit = heap_limit;
    r.status = call_builtin(builtin_spec(id), context, args.data(), static_cast<std::uint32_t>(args.size()), r.out,
                            r.error);
    return r;
}

Value s(const char* text) { return make_string(text); }
Value i(std::int64_t v) { return Value::integer(v); }
Value d(double v) { return Value::number(v); }

Value arr(std::initializer_list<Value> items) {
    Value a = make_array();
    for (const Value& v : items) array_push(a, v);
    return a;
}

Value obj(std::initializer_list<std::pair<const char*, Value>> entries) {
    Value o = make_object();
    for (const auto& e : entries) object_set(o, atom(e.first), e.second);
    return o;
}

bool ok(const Result& r, const std::string& expected) {
    if (r.status != Status::Ok) {
        std::fprintf(stderr, "  expected %s, got error %s\n", expected.c_str(), r.error.c_str());
        return false;
    }
    const std::string got = shown(r.out);
    if (got != "ok " + expected) std::fprintf(stderr, "  expected ok %s, got %s\n", expected.c_str(), got.c_str());
    return got == "ok " + expected;
}

bool err(const Result& r, const std::string& message) {
    if (r.status != Status::Error || r.error != message)
        std::fprintf(stderr, "  expected error '%s', got status %d '%s'\n", message.c_str(), static_cast<int>(r.status),
                     r.error.c_str());
    return r.status == Status::Error && r.error == message;
}

void table() {
    std::set<std::pair<int, std::string>> seen;
    for (std::size_t k = 0; k < builtin_count(); ++k) {
        const BuiltinSpec& spec = builtin_spec(static_cast<BuiltinId>(k));
        CHECK(static_cast<std::size_t>(spec.id) == k);
        CHECK(spec.fn != nullptr);
        CHECK(spec.min_args <= spec.max_args);
        CHECK(spec.guard > 0);
        CHECK(seen.insert({static_cast<int>(spec.receiver), std::string(spec.name)}).second);
        CHECK(find_builtin(spec.receiver, spec.name) == &spec);
    }
    CHECK(find_builtin(Receiver::String, "size") == nullptr);   // legacy: unsupported for strings
    CHECK(find_builtin(Receiver::Array, "index_of") == nullptr);  // arrays spell it indexOf
    CHECK(find_builtin(Receiver::Object, "length") == nullptr);   // context-dependent in legacy (refused)
    CHECK(builtin_count() == 50);
}

void strings() {
    CHECK(ok(call(BuiltinId::StrLength, {s("h\xc3\xa9llo")}), "int | [5]"));
    CHECK(err(call(BuiltinId::StrLength, {s("a\xff")}), "length: invalid UTF-8"));
    CHECK(err(call(BuiltinId::StrLength, {s("a"), i(1)}), "length: expected no arguments"));
    CHECK(ok(call(BuiltinId::StrEmpty, {s("")}), "bool | [true]"));
    CHECK(ok(call(BuiltinId::StrSplit, {s("a,,b,"), s(",")}), "array | [[\"a\",\"\",\"b\",\"\"]]"));
    CHECK(ok(call(BuiltinId::StrSplit, {s("a\xc3\xa9"), s("")}), "array | [[\"a\",\"\xc3\xa9\"]]"));
    CHECK(ok(call(BuiltinId::StrSplit, {s(""), s("")}), "array | [[]]"));
    CHECK(ok(call(BuiltinId::StrSplit, {s(""), s(",")}), "array | [[\"\"]]"));
    CHECK(err(call(BuiltinId::StrSplit, {s("a\xc3"), s("")}), "split: invalid UTF-8"));
    CHECK(err(call(BuiltinId::StrSplit, {s("a"), i(1)}), "split: delimiter must be a string"));
    CHECK(err(call(BuiltinId::StrSplit, {s("a")}), "split: expected one delimiter"));
    CHECK(ok(call(BuiltinId::StrTrim, {s("\v\f ab \t\n")}), "string | [\"\\u000b\\f ab\"]"));
    CHECK(ok(call(BuiltinId::StrTrimStart, {s("\v\f ab \v")}), "string | [\"ab \\u000b\"]"));
    CHECK(ok(call(BuiltinId::StrTrimEnd, {s("\v ab \v\f")}), "string | [\"\\u000b ab\"]"));
    CHECK(ok(call(BuiltinId::StrToUpper, {s("a\xc3\xa9z")}), "string | [\"A\xc3\xa9Z\"]"));
    CHECK(ok(call(BuiltinId::StrToLower, {s("A\xc3\x89Z")}), "string | [\"a\xc3\x89z\"]"));
    CHECK(ok(call(BuiltinId::StrIndexOf, {s("\xc3\xa9t\xc3\xa9"), s("t")}), "int | [2]"));  // bytes
    CHECK(ok(call(BuiltinId::StrIndexOf, {s("abc"), s("z")}), "int | [-1]"));
    CHECK(ok(call(BuiltinId::StrLastIndexOf, {s("abcab"), s("ab")}), "int | [3]"));
    CHECK(ok(call(BuiltinId::StrLastIndexOf, {s("abc"), s("")}), "int | [3]"));
    CHECK(ok(call(BuiltinId::StrContains, {s("abc"), s("")}), "bool | [true]"));
    CHECK(err(call(BuiltinId::StrContains, {s("abc"), i(1)}), "contains: argument must be a string"));
    CHECK(err(call(BuiltinId::StrStartsWith, {s("abc")}), "starts_with: expected one string"));
    CHECK(ok(call(BuiltinId::StrStartsWith, {s("abc"), s("abcd")}), "bool | [false]"));
    CHECK(ok(call(BuiltinId::StrEndsWith, {s("abc"), s("bc")}), "bool | [true]"));
    CHECK(ok(call(BuiltinId::StrEndsWith, {s("bc"), s("abc")}), "bool | [false]"));
    CHECK(ok(call(BuiltinId::StrReplace, {s("aaa"), s("a"), s("aa")}), "string | [\"aaaaaa\"]"));
    CHECK(ok(call(BuiltinId::StrReplace, {s("aaaa"), s("aa"), s("b")}), "string | [\"bb\"]"));
    CHECK(err(call(BuiltinId::StrReplace, {s("a"), s(""), s("b")}), "replace: old string must not be empty"));
    CHECK(err(call(BuiltinId::StrReplace, {s("a"), s("a"), i(1)}), "replace: arguments must be strings"));
    CHECK(err(call(BuiltinId::StrReplace, {s("a"), s("a")}), "replace: expected old and replacement strings"));
    CHECK(ok(call(BuiltinId::StrSubstr, {s("hello"), i(1), i(3)}), "string | [\"ell\"]"));
    CHECK(ok(call(BuiltinId::StrSubstr, {s("hello"), i(99)}), "string | [\"\"]"));
    CHECK(ok(call(BuiltinId::StrSubstr, {s("hello"), d(-0.0)}), "string | [\"hello\"]"));
    CHECK(err(call(BuiltinId::StrSubstr, {s("hello"), d(1.5)}), "substr: invalid pos"));
    CHECK(err(call(BuiltinId::StrSubstr, {s("hello"), i(-1)}), "substr: invalid pos"));
    CHECK(err(call(BuiltinId::StrSubstr, {s("hello"), i(1), s("x")}), "substr: invalid length"));
    CHECK(err(call(BuiltinId::StrSubstr, {s("hello")}), "substr: expected pos and optional length"));
    CHECK(ok(call(BuiltinId::StrToInt, {s("+42")}), "int | [42]"));
    CHECK(ok(call(BuiltinId::StrToInt, {s("+-5")}), "int | [-5]"));
    CHECK(ok(call(BuiltinId::StrToInt, {s("9007199254740993")}), "int | [9007199254740993]"));
    CHECK(err(call(BuiltinId::StrToInt, {s("+")}), "to_int: invalid integer"));
    CHECK(err(call(BuiltinId::StrToInt, {s("")}), "to_int: invalid integer"));
    CHECK(err(call(BuiltinId::StrToInt, {s("9223372036854775808")}), "to_int: invalid or out-of-range integer"));
    CHECK(ok(call(BuiltinId::StrToDouble, {s("2.50")}), "float | [2.5]"));
    CHECK(ok(call(BuiltinId::StrToDouble, {s("1e3")}), "int | [1000]"));
    CHECK(err(call(BuiltinId::StrToDouble, {s("0x10")}), "to_double: invalid number"));
    CHECK(err(call(BuiltinId::StrToDouble, {s(" 1")}), "to_double: invalid number"));
    CHECK(err(call(BuiltinId::StrToDouble, {s("inf")}), "to_double: invalid or out-of-range number"));
    CHECK(err(call(BuiltinId::StrToDouble, {s("1e-400")}), "to_double: invalid or out-of-range number"));
    CHECK(ok(call(BuiltinId::StrToString, {s("x")}), "string | [\"x\"]"));
    // Kinds the compiler never passes are internal failures, never guessed.
    CHECK(call(BuiltinId::StrLength, {i(1)}).status == Status::Internal);
    CHECK(call(BuiltinId::StrTrim, {}).status == Status::Internal);
}

void arrays() {
    const Value a = arr({i(3), i(1), s("x"), d(2.5), Value(), Value::boolean(true), arr({i(1), i(2)}),
                         obj({{"k", i(1)}}), i(3)});
    CHECK(ok(call(BuiltinId::ArrSize, {a}), "int | [9]"));
    CHECK(ok(call(BuiltinId::ArrLength, {a}), "int | [9]"));
    CHECK(ok(call(BuiltinId::ArrEmpty, {arr({})}), "bool | [true]"));
    CHECK(ok(call(BuiltinId::ArrFirst, {a}), "int | [3]"));
    CHECK(err(call(BuiltinId::ArrLast, {arr({})}), "last: array is empty"));
    CHECK(err(call(BuiltinId::ArrFirst, {a, i(1)}), "first: expected no arguments"));
    CHECK(ok(call(BuiltinId::ArrContains, {a, arr({d(1.0), i(2)})}), "bool | [true]"));
    CHECK(ok(call(BuiltinId::ArrIndexOf, {a, obj({{"k", d(1.0)}})}), "int | [7]"));
    CHECK(ok(call(BuiltinId::ArrIndexOf, {a, i(99)}), "int | [-1]"));
    CHECK(err(call(BuiltinId::ArrIndexOf, {a}), "indexOf: expected one value"));
    CHECK(ok(call(BuiltinId::ArrJoin, {arr({i(1), s("a"), Value::boolean(true), Value(), d(2.5)}), s(",")}),
             "string | [\"1,a,true,null,2.5\"]"));
    CHECK(err(call(BuiltinId::ArrJoin, {a, s(",")}), "join: elements must be renderable scalar values"));
    CHECK(err(call(BuiltinId::ArrJoin, {a, i(1)}), "join: separator must be a string"));
    CHECK(ok(call(BuiltinId::ArrSlice, {arr({i(1), i(2), i(3)}), i(3), i(1)}), "array | [[]]"));
    CHECK(ok(call(BuiltinId::ArrSlice, {arr({i(1), i(2), i(3)}), i(1)}), "array | [[2,3]]"));
    CHECK(err(call(BuiltinId::ArrSlice, {a, i(-1)}), "slice: invalid start"));
    CHECK(err(call(BuiltinId::ArrSlice, {a, i(0), d(0.5)}), "slice: invalid end"));
    CHECK(ok(call(BuiltinId::ArrUnique, {arr({i(1), d(1.0), s("1"), arr({i(1)}), arr({d(1.0)}), obj({{"a", i(1)}}),
                                              obj({{"a", i(1)}})})}),
             "array | [[1,\"1\",[1],{\"a\":1}]]"));
    CHECK(ok(call(BuiltinId::ArrFlatten, {arr({arr({i(1), arr({i(2)})}), i(3)})}), "array | [[1,[2],3]]"));
    CHECK(ok(call(BuiltinId::ArrSum, {arr({})}), "int | [0]"));
    CHECK(ok(call(BuiltinId::ArrSum, {arr({i(9007199254740993), i(1), d(0.5)})}), "int | [9007199254740992]"));
    CHECK(err(call(BuiltinId::ArrSum, {a}), "sum: values must be numeric"));
    CHECK(ok(call(BuiltinId::ArrMin, {arr({i(9007199254740993), i(1), d(0.5)})}), "float | [0.5]"));
    CHECK(ok(call(BuiltinId::ArrMax, {arr({i(9007199254740993), i(9007199254740992)})}), "int | [9007199254740993]"));
    CHECK(ok(call(BuiltinId::ArrMax, {arr({s("b"), s("c"), s("a")})}), "string | [\"c\"]"));
    CHECK(ok(call(BuiltinId::ArrMin, {arr({Value::boolean(true)})}), "bool | [true]"));  // no comparison made
    CHECK(err(call(BuiltinId::ArrMin, {arr({})}), "min: cannot aggregate an empty array"));
    CHECK(err(call(BuiltinId::ArrMax, {arr({i(1), s("a")})}), "max: values must be comparable and homogeneous"));
    CHECK(ok(call(BuiltinId::ArrTake, {arr({i(1), i(2), i(3)}), d(1e19)}), "array | [[1,2,3]]"));
    CHECK(ok(call(BuiltinId::ArrDrop, {arr({i(1), i(2), i(3)}), i(2)}), "array | [[3]]"));
    CHECK(err(call(BuiltinId::ArrTake, {a, i(-1)}), "take: count must be a non-negative integer"));
    CHECK(err(call(BuiltinId::ArrTake, {a}), "take: expected one count"));
    CHECK(ok(call(BuiltinId::ArrChunk, {arr({i(1), i(2), i(3)}), i(2)}), "array | [[[1,2],[3]]]"));
    CHECK(err(call(BuiltinId::ArrChunk, {a, i(0)}), "chunk: count must be a positive integer"));
    CHECK(ok(call(BuiltinId::ArrFromEntries, {arr({obj({{"key", s("a")}, {"value", i(1)}}),
                                                   obj({{"value", arr({})}, {"key", d(2.5)}})})}),
             "object | [{\"a\":1,\"2.5\":[]}]"));
    CHECK(err(call(BuiltinId::ArrFromEntries, {arr({obj({{"key", s("a")}, {"value", i(1)}}),
                                                    obj({{"key", s("a")}, {"value", i(2)}})})}),
              "from_entries: duplicate generated key 'a'"));
    CHECK(err(call(BuiltinId::ArrFromEntries, {arr({obj({{"key", s("a")}})})}),
              "from_entries: each entry must be an object containing exactly key and value"));
    CHECK(err(call(BuiltinId::ArrFromEntries, {arr({obj({{"key", Value()}, {"value", i(1)}})})}),
              "from_entries: generated key must be string, number, or bool"));
    CHECK(ok(call(BuiltinId::FnMin, {i(3), i(1), i(2)}), "int | [1]"));
    CHECK(ok(call(BuiltinId::FnMax, {arr({i(3), i(10)})}), "int | [10]"));
    CHECK(ok(call(BuiltinId::FnMax, {s("a")}), "string | [\"a\"]"));
    CHECK(err(call(BuiltinId::FnMin, {arr({})}), "min: expected at least one value"));
    CHECK(err(call(BuiltinId::FnMax, {i(1), s("a")}), "max: values must be comparable and homogeneous"));
    CHECK(ok(call(BuiltinId::FnRange, {i(3)}), "array | [[0,1,2]]"));
    CHECK(ok(call(BuiltinId::FnRange, {i(10), i(0), i(-3)}), "array | [[10,7,4,1]]"));
    CHECK(ok(call(BuiltinId::FnRange, {i(9007199254740993), i(9007199254740995)}),
             "array | [[9007199254740992,9007199254740994]]"));
    CHECK(err(call(BuiltinId::FnRange, {i(1), i(5), i(0)}), "range: step must not be zero"));
    CHECK(err(call(BuiltinId::FnRange, {d(1.5)}), "range: arguments must be signed 64-bit integers"));
    CHECK(err(call(BuiltinId::FnRange, {}), "range: expected stop, start/stop, or start/stop/step"));
    CHECK(err(call(BuiltinId::FnRange, {i(INT64_MAX - 1), i(INT64_MAX), i(5)}), "range: integer overflow"));
#if !defined(__SANITIZE_ADDRESS__)
    // 160 MB of values; skipped under ASan (shared 2-vCPU host).
    CHECK(err(call(BuiltinId::FnRange, {i(10000001)}), "range: result exceeds 10000000 items"));
#endif
    // The heap budget stops a large result while it is built.
    CHECK(call(BuiltinId::FnRange, {i(1000000)}, 1 << 20).status == Status::Memory);
    CHECK(call(BuiltinId::StrSplit, {make_string(std::string(200000, 'a')), s("")}, 1 << 20).status == Status::Memory);
}

void objects() {
    const Value o = obj({{"b", i(1)}, {"a", arr({i(2)})}, {"c", obj({{"d", i(3)}})}});
    CHECK(ok(call(BuiltinId::ObjSize, {o}), "int | [3]"));
    CHECK(ok(call(BuiltinId::ObjEmpty, {make_object()}), "bool | [true]"));
    CHECK(ok(call(BuiltinId::ObjKeys, {o}), "array | [[\"b\",\"a\",\"c\"]]"));
    CHECK(ok(call(BuiltinId::ObjValues, {o}), "array | [[1,[2],{\"d\":3}]]"));
    CHECK(ok(call(BuiltinId::ObjEntries, {obj({{"x", i(1)}})}), "array | [[{\"key\":\"x\",\"value\":1}]]"));
    CHECK(err(call(BuiltinId::ObjKeys, {o, i(1)}), "keys: expected no arguments"));
    CHECK(ok(call(BuiltinId::ObjHas, {o, s("a")}), "bool | [true]"));
    CHECK(ok(call(BuiltinId::ObjHas, {o, s("never-interned-key")}), "bool | [false]"));
    CHECK(err(call(BuiltinId::ObjHas, {o, i(1)}), "has: key must be a string"));
    CHECK(err(call(BuiltinId::ObjHas, {o}), "has: expected key"));
    CHECK(ok(call(BuiltinId::ObjGet, {o, s("c")}), "object | [{\"d\":3}]"));
    CHECK(ok(call(BuiltinId::ObjGet, {o, s("z"), i(5)}), "int | [5]"));
    CHECK(ok(call(BuiltinId::ObjGet, {o, s("z")}), "null | [null]"));
    CHECK(err(call(BuiltinId::ObjGet, {o}), "get: expected key and optional default"));
    CHECK(ok(call(BuiltinId::ObjMerge, {o, obj({{"a", i(9)}, {"e", i(1)}})}),
             "object | [{\"b\":1,\"a\":9,\"c\":{\"d\":3},\"e\":1}]"));
    CHECK(err(call(BuiltinId::ObjMerge, {o, arr({})}), "merge: argument must be an object"));
    CHECK(ok(call(BuiltinId::ObjMergeDeep, {o, obj({{"c", obj({{"x", i(1)}})}, {"b", obj({{"y", i(2)}})}})}),
             "object | [{\"b\":{\"y\":2},\"a\":[2],\"c\":{\"d\":3,\"x\":1}}]"));
    CHECK(ok(call(BuiltinId::ObjPick, {o, arr({s("c"), s("a"), s("zz"), s("c")})}),
             "object | [{\"c\":{\"d\":3},\"a\":[2]}]"));
    CHECK(ok(call(BuiltinId::ObjPick, {o, arr({s("?")})}), "object | [{\"b\":1,\"a\":[2],\"c\":{\"d\":3}}]"));
    CHECK(ok(call(BuiltinId::ObjOmit, {o, arr({s("a"), s("*x")})}), "object | [{\"b\":1,\"c\":{\"d\":3}}]"));
    CHECK(err(call(BuiltinId::ObjPick, {o, s("a")}), "pick: keys must be an array"));
    CHECK(err(call(BuiltinId::ObjOmit, {o, arr({i(1)})}), "omit: keys must be strings"));
    // merge_deep stops at legacy's depth.
    Value deep = make_object();
    for (int k = 0; k < 70; ++k) { Value next = make_object(); object_set(next, atom("n"), deep); deep = next; }
    CHECK(err(call(BuiltinId::ObjMergeDeep, {deep, deep}), "merge_deep: maximum merge depth exceeded"));
    // Globs: legacy's dot-file rule and escapes.
    CHECK(glob_match("*", "abc") && !glob_match("*", ".h") && glob_match(".*", ".h"));
    CHECK(glob_match("a\\*", "a*") && !glob_match("a\\*", "ab") && glob_match("a?c", "abc"));
    CHECK(glob_magic("a*") && !glob_magic("a\\*") && !glob_magic("abc"));
}

// Receivers are never changed, `out` may alias the receiver's register, and
// self-referential calls (o.merge(o)) respect the CP09 reference contract
// (enforced by relocation under ASan).
void aliasing_and_cow() {
    Value o = obj({{"a", obj({{"x", i(1)}})}, {"b", i(2)}});
    Value copy = o;
    Value args[2] = {o, o};
    std::string error;
    Context context;
    CHECK(call_builtin(builtin_spec(BuiltinId::ObjMerge), context, args, 2, args[0], error) == Status::Ok);
    CHECK(equal(o, copy) && shown(args[0]) == shown(o));
    Value deep_args[2] = {o, o};
    CHECK(call_builtin(builtin_spec(BuiltinId::ObjMergeDeep), context, deep_args, 2, deep_args[1], error) ==
          Status::Ok);
    CHECK(shown(deep_args[1]) == shown(o));
    Value a = arr({arr({i(1)}), i(2)});
    array_push(a, a);  // a = [[1], 2, [[1], 2]]
    Value flat_args[1] = {a};
    CHECK(call_builtin(builtin_spec(BuiltinId::ArrFlatten), context, flat_args, 1, flat_args[0], error) == Status::Ok);
    CHECK(shown(flat_args[0]) == "ok array | [[1,2,[1],2]]");
    CHECK(shown(a) == "ok array | [[[1],2,[[1],2]]]");
    Value first_args[1] = {arr({s("long string beyond inline capacity")})};
    CHECK(call_builtin(builtin_spec(BuiltinId::ArrFirst), context, first_args, 1, first_args[0], error) == Status::Ok);
    CHECK(shown(first_args[0]) == "ok string | [\"long string beyond inline capacity\"]");
    Value text_args[3] = {make_string(std::string(40, 'q')), s("q"), s("rr")};
    CHECK(call_builtin(builtin_spec(BuiltinId::StrReplace), context, text_args, 3, text_args[0], error) == Status::Ok);
    CHECK(string_view_of(text_args[0]) == std::string(80, 'r'));
    // The shared result of an unchanged trim is the receiver itself (no copy).
    Value t = make_string(std::string(30, 'x'));
    Result r = call(BuiltinId::StrTrim, {t});
    CHECK(r.out.heap() == t.heap());
}

// Every native leaves the live-heap total equal to the exact footprint of the
// values alive afterwards, and back at its start once they are released.
void heap_accounting() {
    const std::size_t base = live_heap_bytes();
    {
        Value text = make_string("alpha,beta,gamma,delta,epsilon,zeta,eta,theta");
        Value numbers = make_array();
        for (int k = 0; k < 300; ++k) array_push(numbers, Value::integer(k % 17));
        Value o = obj({{"a", arr({i(1)})}, {"b", s("a long string value outside the inline form")}});
        std::vector<Result> results;
        results.push_back(call(BuiltinId::StrSplit, {text, s(",")}));
        results.push_back(call(BuiltinId::StrSplit, {text, s("")}));
        results.push_back(call(BuiltinId::StrReplace, {text, s("a"), s("AAAA")}));
        results.push_back(call(BuiltinId::StrToUpper, {text}));
        results.push_back(call(BuiltinId::ArrUnique, {numbers}));
        results.push_back(call(BuiltinId::ArrChunk, {numbers, i(7)}));
        results.push_back(call(BuiltinId::ArrSlice, {numbers, i(5), i(250)}));
        results.push_back(call(BuiltinId::ArrJoin, {numbers, s(", ")}));
        results.push_back(call(BuiltinId::ArrFlatten, {results[5].out}));
        results.push_back(call(BuiltinId::FnRange, {i(5000)}));
        results.push_back(call(BuiltinId::ObjEntries, {o}));
        results.push_back(call(BuiltinId::ObjMergeDeep, {o, obj({{"a", obj({})}, {"c", o}})}));
        results.push_back(call(BuiltinId::ObjPick, {o, arr({s("*")})}));
        std::vector<const Value*> roots = {&text, &numbers, &o};
        for (const Result& r : results) {
            CHECK(r.status == Status::Ok);
            roots.push_back(&r.out);
        }
        CHECK(live_heap_bytes() - base == builtins_test::footprint(roots));
    }
    CHECK(live_heap_bytes() == base);
}

} // namespace

int main() {
    table();
    strings();
    arrays();
    objects();
    aliasing_and_cow();
    heap_accounting();
    return rt_test::finish("builtins-unit");
}
