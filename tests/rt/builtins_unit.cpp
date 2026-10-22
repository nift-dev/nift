// CP14 built-in natives: unit tests (results and legacy messages for every
// native, table invariants, receiver immutability and output aliasing,
// exact heap accounting, the CP09 reference contract under relocation).
#include "builtins_support.h"
#include "rt_test.h"

#include "rt/compare.h"
#include "rt/decimal.h"
#include "rt/error.h"
#include "rt/heap.h"
#include "rt/natives/bytes_methods.h"
#include "rt/natives/format_methods.h"
#include "rt/natives/json_methods.h"
#include "rt/natives/object_methods.h"
#include "rt/natives/support.h"

#include <cmath>
#include <cstring>
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

Result call(BuiltinId id, std::initializer_list<Value> list, std::size_t heap_limit = Context::kNoHeapLimit) {
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
    // 89 CP14/CP15 built-ins plus the 31 CP16 Host built-ins (builtin_list.inc).
    CHECK(builtin_count() == 120);
    CHECK(find_builtin(Receiver::Number, "to_string") && find_builtin(Receiver::Any, "stringify"));
    CHECK(find_builtin(Receiver::Number, "to_double") == nullptr);  // Q4: context-dependent in legacy
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

void formatting() {
    const Value o = obj({{"b", i(1)}, {"a", arr({i(2), s("q\"\x01\n\x7f")})}, {"c", obj({})}, {"d", arr({})}});
    CHECK(ok(call(BuiltinId::Stringify, {o}),
             "string | [\"{\\\"b\\\":1,\\\"a\\\":[2,\\\"q\\\\\\\"\\\\u0001\\\\n\x7f\\\"],"
             "\\\"c\\\":{},\\\"d\\\":[]}\"]"));
    Value text;
    std::string error;
    bool bad = false;
    std::string out;
    CHECK(serialize(o, true, out, error, bad));
    CHECK(out == "{\n  \"b\": 1,\n  \"a\": [\n    2,\n    \"q\\\"\\u0001\\n\x7f\"\n  ],\n  \"c\": {},\n  \"d\": []\n}");
    out.clear();
    CHECK(serialize(s("x"), false, out, error, bad) && out == "\"x\"");
    CHECK(ok(call(BuiltinId::Highlight, {arr({d(2.5), Value()})}), "string | [\"[2.5,null]\"]"));
    CHECK(err(call(BuiltinId::Stringify, {make_bytes({1})}), "bytes values are not serializable"));
    CHECK(err(call(BuiltinId::Stringify, {o, i(1)}), "stringify: expected no arguments"));
    Value deep = arr({});
    for (int k = 0; k < 129; ++k) deep = arr({deep});
    CHECK(err(call(BuiltinId::Stringify, {deep}), "value serialization depth exceeded"));
    CHECK(ok(call(BuiltinId::Stringify, {array_items(deep)[0]}), "string | [\"" + std::string(129, '[') +
             std::string(129, ']') + "\"]"));
    CHECK(ok(call(BuiltinId::NumToString, {d(1e21)}), "string | [\"1e+21\"]"));
    CHECK(ok(call(BuiltinId::NumToString, {d(-0.0)}), "string | [\"0\"]"));
    CHECK(ok(call(BuiltinId::NumToString, {i(9007199254740993)}), "string | [\"9007199254740993\"]"));
    CHECK(ok(call(BuiltinId::BoolToString, {Value::boolean(false)}), "string | [\"false\"]"));
    CHECK(ok(call(BuiltinId::NullToString, {Value()}), "string | [\"null\"]"));
    CHECK(err(call(BuiltinId::NullToString, {Value(), i(1)}), "to_string: expected no arguments"));
    CHECK(ok(call(BuiltinId::FnHtmlEscape, {s("<a href='x'>&\"")}), "string | [\"&lt;a href='x'&gt;&amp;\\\"\"]"));
    CHECK(ok(call(BuiltinId::FnAttrEscape, {s("<'\">")}), "string | [\"&lt;&#39;&quot;&gt;\"]"));
    CHECK(ok(call(BuiltinId::FnUrlEncode, {s("a b/\xc3\xa9~")}), "string | [\"a%20b%2F%C3%A9~\"]"));
    CHECK(ok(call(BuiltinId::FnUrlEncode, {Value()}), "string | [\"null\"]"));
    CHECK(err(call(BuiltinId::FnHtmlEscape, {arr({})}), "html_escape: value must be a string or scalar"));
    CHECK(err(call(BuiltinId::FnUrlEncode, {s("a"), s("b")}), "url_encode: expected one value"));
    CHECK(ok(call(BuiltinId::FnType, {d(2.0)}), "string | [\"int\"]"));
    CHECK(ok(call(BuiltinId::FnType, {d(2.5)}), "string | [\"float\"]"));
    CHECK(err(call(BuiltinId::FnType, {}), "type: expected one value"));
    CHECK(ok(call(BuiltinId::FnIsNumber, {s("1")}), "bool | [false]"));
    CHECK(ok(call(BuiltinId::FnIsInt, {d(2.0)}), "bool | [true]"));
    CHECK(ok(call(BuiltinId::FnIsFloat, {i(2)}), "bool | [false]"));
    CHECK(ok(call(BuiltinId::FnIsObject, {o}), "bool | [true]"));
    CHECK(ok(call(BuiltinId::FnIsEnum, {i(1)}), "bool | [false]"));
    CHECK(err(call(BuiltinId::FnIsBool, {}), "is_bool: expected one value"));
    CHECK(call(BuiltinId::FnType, {Value::handle(HandleKind::File, 1, 0)}).status == Status::Internal);
    // Number rendering is a port of legacy dump: compared with the legacy
    // engine on special values and 200,000 random bit patterns.
    std::uint64_t state = 88172645463325252ull;
    auto next = [&] { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; };
    std::vector<double> samples = {0.0, -0.0, 1e21, 1e-7, 0.1, 123.456, 9007199254740993.0, 9.2233720368547758e18,
                                   -9.2233720368547758e18, 1e300, 5e-324, 1.7976931348623157e308, -2.5, 3.0};
    for (int k = 0; k < 200000; ++k) {
        std::uint64_t bits = next();
        double v;
        std::memcpy(&v, &bits, 8);
        if (k % 2) v = static_cast<double>(static_cast<std::int64_t>(bits >> (bits & 63)));
        if (std::isfinite(v)) samples.push_back(v);
    }
    long mismatches = 0;
    for (double v : samples) {
        std::string mine;
        append_number(Value::number(v), mine);
        if (mine != nift::RuntimeValue(v).dump(0) && ++mismatches <= 5)
            std::fprintf(stderr, "  number %.17g: native %s legacy %s\n", v, mine.c_str(), nift::RuntimeValue(v).dump(0).c_str());
    }
    CHECK(mismatches == 0);
}

void batch_b() {
    // N1 numbers
    CHECK(ok(call(BuiltinId::NumAbs, {d(-2.5)}), "float | [2.5]"));
    CHECK(ok(call(BuiltinId::NumAbs, {i(-9007199254740993)}), "int | [9007199254740992]"));  // via double (N-Q1)
    CHECK(ok(call(BuiltinId::NumFloor, {d(-2.5)}), "int | [-3]"));
    CHECK(ok(call(BuiltinId::NumCeil, {d(-2.5)}), "int | [-2]"));
    CHECK(ok(call(BuiltinId::NumRound, {d(-2.5)}), "int | [-3]"));
    CHECK(ok(call(BuiltinId::NumRound, {d(0.5)}), "int | [1]"));
    CHECK(err(call(BuiltinId::NumRound, {d(0.5), i(0)}), "round: expected no arguments"));
    CHECK(ok(call(BuiltinId::NumToInt, {i(9007199254740993)}), "int | [9007199254740993]"));
    CHECK(ok(call(BuiltinId::NumToInt, {d(-3.0)}), "int | [-3]"));
    CHECK(err(call(BuiltinId::NumToInt, {d(2.5)}), "to_int: invalid or out-of-range integer"));
    CHECK(err(call(BuiltinId::NumToInt, {d(1e19)}), "to_int: invalid or out-of-range integer"));
    CHECK(call(BuiltinId::NumAbs, {s("1")}).status == Status::Internal);
    // B1 bytes
    Result b = call(BuiltinId::FnBytes, {arr({i(97), d(98.0), i(0), i(255)})});
    CHECK(b.status == Status::Ok && b.out.is(Kind::Bytes) && bytes_of(b.out).size() == 4 && bytes_of(b.out)[3] == 255);
    CHECK(call(BuiltinId::FnBytes, {}).status == Status::Ok);
    CHECK(err(call(BuiltinId::FnBytes, {arr({i(256)})}), "bytes: array values must be integers from 0 to 255"));
    CHECK(err(call(BuiltinId::FnBytes, {arr({d(1.5)})}), "bytes: array values must be integers from 0 to 255"));
    CHECK(err(call(BuiltinId::FnBytes, {s("ab")}), "bytes: expected an array of integers"));
    CHECK(err(call(BuiltinId::FnBytes, {arr({}), arr({})}), "bytes: expected zero arguments or one byte array"));
    CHECK(ok(call(BuiltinId::BytesLength, {b.out}), "int | [4]"));
    CHECK(ok(call(BuiltinId::BytesEmpty, {b.out}), "bool | [false]"));
    Result sl = call(BuiltinId::BytesSlice, {b.out, i(1), i(3)});
    CHECK(sl.status == Status::Ok && bytes_of(sl.out) == std::vector<std::uint8_t>({98, 0}));
    CHECK(err(call(BuiltinId::BytesSlice, {b.out, i(-1)}), "slice: invalid start"));
    Result e = call(BuiltinId::StrEncode, {s("h\xc3\xa9"), s("utf-8")});
    CHECK(e.status == Status::Ok && bytes_of(e.out).size() == 3);
    CHECK(ok(call(BuiltinId::BytesDecode, {e.out, s("utf-8")}), "string | [\"h\xc3\xa9\"]"));
    CHECK(err(call(BuiltinId::StrEncode, {s("a"), s("UTF-8")}), "encode: encoding must be exactly 'utf-8'"));
    CHECK(err(call(BuiltinId::StrEncode, {s("a")}), "encode: expected encoding"));
    CHECK(err(call(BuiltinId::StrEncode, {s("\xed\xa0\x80"), s("utf-8")}), "encode: invalid UTF-8 text"));  // surrogate
    CHECK(ok(call(BuiltinId::StrLength, {s("\xed\xa0\x80")}), "int | [1]"));  // B-Q1: length is lenient
    CHECK(err(call(BuiltinId::BytesDecode, {make_bytes({0xff}), s("utf-8")}), "decode: invalid UTF-8"));
    CHECK(err(call(BuiltinId::BytesDecode, {make_bytes({0xc0, 0x80}), s("utf-8")}), "decode: invalid UTF-8"));
    CHECK(err(call(BuiltinId::BytesDecode, {make_bytes({}), s("latin1")}), "decode: encoding must be exactly 'utf-8'"));
    // Empty bytes (data() may be null): decode and sha256 (UBSan, review round 1).
    CHECK(ok(call(BuiltinId::BytesDecode, {make_bytes({}), s("utf-8")}), "string | [\"\"]"));
    CHECK(ok(call(BuiltinId::FnSha256, {make_bytes({})}),
             "string | [\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"]"));
    CHECK(ok(call(BuiltinId::FnSha256, {s("abc")}),
             "string | [\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"]"));
    CHECK(ok(call(BuiltinId::FnSha256, {make_bytes({97, 98, 99})}),
             "string | [\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"]"));
    CHECK(err(call(BuiltinId::FnSha256, {i(1)}), "sha256: expected a string or bytes value"));
    CHECK(err(call(BuiltinId::FnSha256, {}), "sha256: expected one string or bytes value"));
    CHECK(err(call(BuiltinId::Stringify, {arr({b.out})}), "bytes values are not serializable"));
    CHECK(ok(call(BuiltinId::FnType, {b.out}), "string | [\"bytes\"]"));
    // J1 JSON values
    Context context;
    Value v;
    std::string error;
    CHECK(json_parse(context, "[9007199254740993, 0.0, -0, 1E2, 1e400, \"\\u00e9\\n\", {\"a\": [true, null]}]", v,
                     error) == Status::Error);  // 1e400: not finite
    CHECK(error.find("finite") != std::string::npos);
    CHECK(json_parse(context, "[9007199254740993, 0.0, -0, 1E2, 1e-400, \"\\u00e9\\n\", {\"a\": [true, null]}]", v,
                     error) == Status::Ok);
    CHECK(shown(v) == "ok array | [[9007199254740993,0.0,-0,100,1e-400,\"\xc3\xa9\\n\",{\"a\":[true,null]}]]");
    CHECK(array_items(v)[1].is(Kind::Decimal) && array_items(v)[3].is(Kind::Double));
    CHECK(json_parse(context, "{\"a\": 1, \"a\": 2}", v, error) == Status::Error);
    CHECK(json_parse(context, "[1,]", v, error) == Status::Error);
    const Value schema = obj({{"type", s("string")}});
    CHECK(ok(call(BuiltinId::FnValidate, {schema, s("x")}), "string | [\"x\"]"));
    Result rejected = call(BuiltinId::FnValidate, {schema, i(1)});
    CHECK(rejected.status == Status::Recoverable && rejected.error == "validate: at $: expected string, received number");
    CHECK(err(call(BuiltinId::FnValidate, {obj({{"type", i(1)}}), i(1)}),
              "validate: type must be a string or array of strings at #"));
    CHECK(err(call(BuiltinId::FnValidate, {schema, make_bytes({1})}), "validate: bytes values are not JSON serializable"));
    CHECK(err(call(BuiltinId::FnValidate, {schema}), "validate: expected schema and value"));
    const Value nested = arr({arr({i(1)}), obj({{"k", i(2)}})});
    Result copied = call(BuiltinId::FnDeepcopy, {nested});
    CHECK(copied.status == Status::Ok && equal(copied.out, nested));
    array_push(array_mut(copied.out)[0], i(9));  // mutating the copy leaves the original (COW)
    CHECK(shown(nested) == "ok array | [[[1],{\"k\":2}]]");
    // Review D1: functions and Error values are copied as they are.
    const Value fn = Value::adopt(new FunctionObject());
    Result fcopy = call(BuiltinId::FnCopy, {fn});
    CHECK(fcopy.status == Status::Ok && fcopy.out.heap() == fn.heap());
    const Value raised = make_user_error("boom");
    Result ecopy = call(BuiltinId::FnDeepcopy, {arr({raised, fn})});
    CHECK(ecopy.status == Status::Ok);
    // Review D2: a struct or handle anywhere in the value is a compiler defect.
    const Value point = Value::adopt(new StructObject(atom("Point")));
    CHECK(call(BuiltinId::FnCopy, {point}).status == Status::Internal);
    CHECK(call(BuiltinId::FnCopy, {arr({i(1), obj({{"p", point}})})}).status == Status::Internal);
    CHECK(call(BuiltinId::FnDeepcopy, {obj({{"h", Value::handle(HandleKind::File, 1, 0)}})}).status ==
          Status::Internal);
    // Review R2: validate stops beyond 512 nested levels (Limit), at exactly 512 it validates.
    auto nest = [](int depth) {
        Value v = arr({});
        for (int k = 0; k < depth; ++k) v = arr({v});
        return v;
    };
    const Value any_schema = make_object();
    CHECK(call(BuiltinId::FnValidate, {any_schema, nest(512)}).status == Status::Ok);
    Result deep = call(BuiltinId::FnValidate, {any_schema, nest(513)});
    CHECK(deep.status == Status::Limit && deep.error == "validate: value nesting exceeds 512 levels");
    CHECK(call(BuiltinId::FnValidate, {any_schema, nest(200000)}).status == Status::Limit);  // legacy crashes
    CHECK(recoverable_code(BuiltinId::FnValidate) == "schema.rejected" && recoverable_code(BuiltinId::FnCopy).empty());
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
    formatting();
    batch_b();
    aliasing_and_cow();
    heap_accounting();
    return rt_test::finish("builtins-unit");
}
