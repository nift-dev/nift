// CP14 built-in natives: randomised differential test against the legacy
// interpreter. Every case calls one native on generated rt values and the
// same built-in in a generated Nift program run by the legacy binary
// (`r := c0.method(c1, ...)`, then `type(r)` and `[r].stringify()`); the
// results (type and JSON) or the error messages must be identical.
//
// Usage: builtins-diff <legacy nift> [seed] [cases]
//   Cases run in batches of one program; a legacy error ends a batch at the
//   failing case, and the batch resumes after it.
#include "builtins_support.h"
#include "rt_test.h"

#include "rt/natives/builtin.h"
#include "rt/natives/json_methods.h"
#include "Sha256.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

using namespace nift::rt;
using namespace nift::rt::natives;

namespace {

std::mt19937_64 rng;
std::size_t pick(std::size_t n) { return static_cast<std::size_t>(rng() % n); }
bool chance(int percent) { return static_cast<int>(rng() % 100) < percent; }

const char* const kPieces[] = {"a", "b", "l", "o", " ", "\t", "\n", "\xc3\xa9", "\xe6\x97\xa5", "\v", "\f", ",", "-",
                               "x", "ab", "A", "Z", "0", "1", "+", ".", "e", "*", "?", "\\", "\"", "ll", "  "};
const char* const kNumeric[] = {"42", "+42", "-7", "+-5", " 4", "4 ", "1e3", "2.50", ".5", "5.", "0x10", "inf",
                                "nan", "1e400", "1e-400", "9223372036854775807", "9223372036854775808", "", "+",
                                "abc", "1_0", "-0", "007", "1E2", "-2.5e-3", "9007199254740993", "0X1", "3\v", "1e"};
const char* const kKeys[] = {"a", "b", "c", "d", "ab", ".h", "a*", "key", "value", "x?"};
const char* const kPatterns[] = {"a", "b", "zz", "*", "?", "a*", "\\*", ".*", "a\\*", "?b", "*b", "x?", "[a]"};
const double kDoubles[] = {0.5, 2.5, -1.25, 3.0, -0.0, 0.1, 1e21, 1e-7, 123.456, -7.0};

std::string random_text(std::size_t max_pieces = 8) {
    std::string out;
    const std::size_t n = pick(max_pieces + 1);
    for (std::size_t k = 0; k < n; ++k) out += kPieces[pick(sizeof kPieces / sizeof kPieces[0])];
    return out;
}

Value parsed(const char* text);

// Review R4: exact Decimals (built by json_parse from spellings legacy keeps
// exactly, and bound in the program through a JSON object literal so legacy
// holds the same StrNumber), the int64 extremes, and halves (round).
const char* const kDecimalSpellings[] = {"-0", "0.0", "-0.0", "0.00", "12345678901234567890", "-12345678901234567890",
                                         "9007199254740993", "18446744073709551615", "1e-400", "-1e-400", "1e300",
                                         "-2.5e300", "100000000000000000000.5", "-9223372036854775809"};
const double kHalves[] = {0.5, 1.5, 2.5, -0.5, -1.5, -2.5};

Value random_number() {
    switch (pick(9)) {
        case 0: return Value::number(kDoubles[pick(sizeof kDoubles / sizeof kDoubles[0])]);
        case 1: return Value::integer(9007199254740993 + static_cast<std::int64_t>(pick(3)));
        case 2: return Value::number(kHalves[pick(sizeof kHalves / sizeof kHalves[0])]);
        case 3: {
            const Value d = parsed(kDecimalSpellings[pick(sizeof kDecimalSpellings / sizeof kDecimalSpellings[0])]);
            return d;
        }
        case 4:
            if (chance(50)) return Value::integer(chance(50) ? INT64_MIN : INT64_MAX);
            return Value::integer(chance(50) ? INT64_MIN + 1 : INT64_MAX - 1);
        default: return Value::integer(static_cast<std::int64_t>(pick(26)) - 5);
    }
}

Value random_scalar() {
    switch (pick(6)) {
        case 0: return Value();
        case 1: return Value::boolean(chance(50));
        case 2: case 3: return random_number();
        default: return make_string(random_text(4));
    }
}

Value random_value(int depth);

Value random_array(int depth, std::size_t max = 6) {
    Value a = make_array();
    const std::size_t n = pick(max + 1);
    for (std::size_t k = 0; k < n; ++k) array_push(a, random_value(depth + 1));
    return a;
}

Value random_object(int depth, std::size_t max = 5) {
    Value o = make_object();
    const std::size_t n = pick(max + 1);
    for (std::size_t k = 0; k < n; ++k)
        object_set(o, atom(kKeys[pick(sizeof kKeys / sizeof kKeys[0])]), random_value(depth + 1));
    return o;
}

Value random_value(int depth) {
    if (depth >= 2 || chance(65)) return random_scalar();
    return chance(50) ? random_array(depth) : random_object(depth);
}

Value homogeneous_array() {
    Value a = make_array();
    const std::size_t n = pick(6);
    const bool strings = chance(35);
    for (std::size_t k = 0; k < n; ++k) array_push(a, strings ? make_string(random_text(3)) : random_number());
    if (chance(10)) array_push(a, random_scalar());
    return a;
}

Value index_argument(std::int64_t size) {
    if (chance(8)) return Value::number(kDoubles[pick(sizeof kDoubles / sizeof kDoubles[0])]);
    if (chance(4)) return make_string("1");
    if (chance(4)) return Value::number(1e19);
    return Value::integer(static_cast<std::int64_t>(pick(static_cast<std::size_t>(size + 5))) - 2);
}

Value text_argument(const Value& receiver) {
    if (chance(6)) return random_scalar();
    if (chance(8)) return make_string("");
    const std::string_view text = string_view_of(receiver);
    if (!text.empty() && chance(60)) {
        const std::size_t a = pick(text.size()), n = 1 + pick(3);
        return make_string(text.substr(a, n));
    }
    return make_string(random_text(2));
}

// Bytes: mostly the UTF-8 encoding of random text (decodable), sometimes
// arbitrary bytes.
Value random_bytes(bool text) {
    std::vector<std::uint8_t> data;
    if (text && chance(80)) {
        const std::string t = random_text();
        data.assign(t.begin(), t.end());
    } else {
        const std::size_t n = pick(9);
        for (std::size_t k = 0; k < n; ++k) data.push_back(static_cast<std::uint8_t>(pick(256)));
    }
    return make_bytes(static_cast<std::vector<std::uint8_t>&&>(data));
}

const char* const kSchemas[] = {
    R"({"type": "string"})", R"({"type": "number"})", R"({"type": ["array", "null"]})",
    R"({"type": "object", "required": ["a"], "properties": {"a": {"type": "integer"}}})",
    R"({"type": "array", "items": {"type": "string"}, "minItems": 1})", R"({"enum": [1, "a", null]})",
    R"({"type": "string", "minLength": 2, "maxLength": 4})", R"({"type": 1})", R"({"minimum": 0, "maximum": 10})",
    R"({"type": "object", "additionalProperties": false, "properties": {"b": {}}})", R"({"const": [1]})",
};

Value parsed(const char* text) {
    Value v;
    std::string error;
    json_parse(Context{}, text, v, error);
    return v;
}

Value entries_array() {
    Value a = make_array();
    const std::size_t n = pick(5);
    for (std::size_t k = 0; k < n; ++k) {
        Value e = make_object();
        if (chance(90)) object_set(e, atom("key"), chance(70) ? make_string(kKeys[pick(4)]) : random_scalar());
        if (chance(90)) object_set(e, atom("value"), random_value(1));
        if (chance(5)) object_set(e, atom("extra"), Value());
        array_push(a, chance(95) ? e : random_scalar());
    }
    return a;
}

// Arguments (receiver first for methods) for one call of `id`.
std::vector<Value> arguments(BuiltinId id) {
    const BuiltinSpec& spec = builtin_spec(id);
    std::vector<Value> args;
    switch (spec.receiver) {
        case Receiver::String:
            args.push_back(id == BuiltinId::StrToInt || id == BuiltinId::StrToDouble
                               ? make_string(chance(85) ? kNumeric[pick(sizeof kNumeric / sizeof kNumeric[0])]
                                                        : random_text(3))
                               : make_string(random_text()));
            break;
        case Receiver::Array:
            if (id == BuiltinId::ArrJoin || id == BuiltinId::ArrSum || id == BuiltinId::ArrMin ||
                id == BuiltinId::ArrMax)
                args.push_back(chance(85) ? homogeneous_array() : random_array(0));
            else if (id == BuiltinId::ArrFromEntries) args.push_back(entries_array());
            else args.push_back(random_array(0, 8));
            break;
        case Receiver::Object: args.push_back(random_object(0)); break;
        case Receiver::Number: args.push_back(random_number()); break;
        case Receiver::Bool: args.push_back(Value::boolean(chance(50))); break;
        case Receiver::Null: args.push_back(Value()); break;
        case Receiver::Any: args.push_back(chance(70) ? random_array(0) : random_value(0)); break;
        case Receiver::Bytes: args.push_back(random_bytes(id == BuiltinId::BytesDecode)); break;
        case Receiver::None: break;
    }
    const Value receiver = args.empty() ? Value() : args[0];
    std::size_t count = spec.min_args + (spec.max_args > spec.min_args && spec.max_args != kVariadic ? pick(spec.max_args - spec.min_args + 1) : 0);
    if (spec.max_args == kVariadic) count = 1 + pick(4);
    if (chance(4)) count = count == 0 ? 1 : count - 1 + 2 * pick(2);  // wrong arity now and then
    for (std::size_t k = 0; k < count; ++k) {
        Value a;
        switch (id) {
            case BuiltinId::StrSplit: case BuiltinId::StrIndexOf: case BuiltinId::StrLastIndexOf:
            case BuiltinId::StrContains: case BuiltinId::StrStartsWith: case BuiltinId::StrEndsWith:
            case BuiltinId::StrReplace:
                a = k == 1 && id == BuiltinId::StrReplace ? make_string(random_text(2)) : text_argument(receiver);
                break;
            case BuiltinId::StrSubstr: a = index_argument(static_cast<std::int64_t>(string_view_of(receiver).size())); break;
            case BuiltinId::ArrSlice: case BuiltinId::ArrTake: case BuiltinId::ArrDrop: case BuiltinId::ArrChunk:
                a = index_argument(static_cast<std::int64_t>(array_size(receiver)));
                break;
            case BuiltinId::ArrIndexOf: case BuiltinId::ArrContains:
                a = array_size(receiver) && chance(60) ? array_items(receiver)[pick(array_size(receiver))]
                                                       : random_value(1);
                break;
            case BuiltinId::ArrJoin: a = chance(90) ? make_string(random_text(2)) : random_scalar(); break;
            case BuiltinId::FnMin: case BuiltinId::FnMax:
                a = count == 1 && chance(60) ? homogeneous_array() : (chance(85) ? random_number() : random_scalar());
                break;
            case BuiltinId::FnRange:
                a = chance(90) ? Value::integer(static_cast<std::int64_t>(pick(21)) - 10) : random_number();
                break;
            case BuiltinId::ObjHas: case BuiltinId::ObjGet:
                a = k == 0 ? (chance(90) ? make_string(kKeys[pick(sizeof kKeys / sizeof kKeys[0])]) : random_scalar())
                           : random_value(1);
                break;
            case BuiltinId::FnHtmlEscape: case BuiltinId::FnAttrEscape: case BuiltinId::FnUrlEncode:
                a = chance(80) ? make_string(random_text() + (chance(30) ? "<a href='x'>&\"/" : "")) : random_value(0);
                break;
            case BuiltinId::FnBytes: {
                a = make_array();
                const std::size_t n = pick(6);
                for (std::size_t j = 0; j < n; ++j)
                    array_push(a, chance(90) ? Value::integer(static_cast<std::int64_t>(pick(256)))
                                             : (chance(50) ? Value::integer(256 + static_cast<std::int64_t>(pick(3)) - 1)
                                                           : random_number()));
                if (chance(25)) array_push(a, Value::integer(chance(50) ? 256 : (chance(50) ? 255 : -1)));  // boundaries
                if (chance(5)) a = random_scalar();
                break;
            }
            case BuiltinId::StrEncode: case BuiltinId::BytesDecode:
                a = chance(85) ? make_string("utf-8") : make_string(chance(50) ? "UTF-8" : "latin1");
                break;
            case BuiltinId::FnSha256: a = chance(45) ? make_string(random_text()) : (chance(80) ? random_bytes(false) : random_scalar()); break;
            case BuiltinId::BytesSlice: a = index_argument(static_cast<std::int64_t>(bytes_of(receiver).size())); break;
            case BuiltinId::FnValidate:
                a = k == 0 && chance(90) ? parsed(kSchemas[pick(sizeof kSchemas / sizeof kSchemas[0])]) : random_value(0);
                break;
            case BuiltinId::ObjMerge: case BuiltinId::ObjMergeDeep:
                a = chance(90) ? random_object(0) : random_value(1);
                break;
            case BuiltinId::ObjPick: case BuiltinId::ObjOmit: {
                a = make_array();
                const std::size_t n = pick(4);
                for (std::size_t j = 0; j < n; ++j)
                    array_push(a, chance(95) ? make_string(kPatterns[pick(sizeof kPatterns / sizeof kPatterns[0])])
                                             : random_scalar());
                if (chance(5)) a = random_scalar();
                break;
            }
            default: a = random_value(0); break;
        }
        args.push_back(a);
    }
    return args;
}

// Random JSON text for json_parse (CP15): number spellings that legacy keeps
// exactly (big integers, zero with fraction or sign, |exponent| >= 300) and
// ordinary ones, string escapes, nesting, unique keys, random spacing.
const char* const kJsonNumbers[] = {"0", "-0", "0.0", "-0.0", "1", "-17", "2.5", "1e2", "1E+2", "-1.5e-3", "1e300",
                                    "1e-300", "9007199254740993", "12345678901234567890123", "123456789012345",
                                    "1234567890123456", "0.1", "100.000", "5e-324", "1.7976931348623157e308",
                                    "-9223372036854775808", "3.0e0"};
const char* const kJsonStrings[] = {"\"\"", "\"a\"", "\"\\u00e9\"", "\"\\n\\t\\\"\\\\\"", "\"\\u0041\\u00df\"",
                                    "\"\xc3\xa9\xe6\x97\xa5\"", "\"\\/\"", "\"\\ud83d\\ude00\"", "\"x y\"",
                                    "\"\\u0001\""};

std::string space() { return chance(30) ? (chance(50) ? " " : "  ") : ""; }

std::string random_json(int depth, bool top) {
    if (!top && (depth >= 3 || chance(55))) {
        switch (pick(5)) {
            case 0: return chance(50) ? "true" : (chance(50) ? "false" : "null");
            case 1: case 2: return kJsonNumbers[pick(sizeof kJsonNumbers / sizeof kJsonNumbers[0])];
            default: return kJsonStrings[pick(sizeof kJsonStrings / sizeof kJsonStrings[0])];
        }
    }
    // Top level: an object. Legacy JSON-parses a structured literal only when
    // it is a JSON object (`{...}`); an array literal's elements are Nift
    // expressions (design record J-Q1), so they are not json_parse cases.
    const bool array = !top && chance(50);
    std::string out = array ? "[" : "{";
    const std::size_t n = pick(5);
    for (std::size_t k = 0; k < n; ++k) {
        out += k ? "," + space() : space();
        if (!array) out += "\"" + std::string(kKeys[k % 6]) + "\"" + space() + ":" + space();
        out += random_json(depth + 1, false);
    }
    return out + space() + (array ? "]" : "}");
}

// Review R4: invalid JSON object literals (balanced, so a broken case cannot
// swallow the rest of a program; each still runs alone). json_parse must
// reject them; the compiler then compiles the literal as a Nift literal
// (W9 option A, legacy's fallback; the CP10/CP11 guards apply), which this
// native-level test cannot evaluate: not compared here, legacy's outcome
// class is tallied (the VM side is checked by tests/runtime2/vm_builtins.py).
std::string corrupt_json(std::string text) {
    const std::size_t open = text.find('{');
    switch (pick(7)) {
        case 0: return text.insert(open + 1, "\"z\": 01, ");                       // leading zero
        case 1: return text.insert(open + 1, "\"z\": \"\\ud800\", ");          // lone surrogate
        case 2: return text.insert(open + 1, "\"z\": \"\xff\", ");              // invalid UTF-8
        case 3: return text.insert(open + 1, "\"z\": 1, \"\\u007a\": 2, ");  // duplicate after unescaping
        case 4: return text.insert(open + 1, "\"z\": [1,], ");                     // trailing comma
        case 5: return text.insert(open + 1, "\"z\": 1e400, ");                    // not finite
        default: {                                                             // depth beyond 512
            std::string deep;
            for (int k = 0; k < 513; ++k) deep += "{\"d\":";
            deep += "1";
            for (int k = 0; k < 513; ++k) deep += "}";
            return text.insert(open + 1, "\"z\": " + deep + ", ");
        }
    }
}

struct Case {
    BuiltinId id;  // BuiltinId::Count: a json_parse case (`json`)
    std::string json;
    bool isolated = false;  // run in a program of its own (corrupted JSON)
    std::vector<Value> args;
    std::string expected;  // the native's outcome
    std::string legacy;    // the legacy outcome
};

// Calls the compiler refuses before execution (wiring rule W1), which the
// differential test therefore does not compare: keys/values/entries with
// arguments are an arity error on most receivers but ignore the arguments on
// a binding (shape-dependent legacy behaviour, design record Q10).
// Every case is generated so the compiler would bind it otherwise.
bool refused(const Case& c) {
    const bool listing = c.id == BuiltinId::ObjKeys || c.id == BuiltinId::ObjValues || c.id == BuiltinId::ObjEntries;
    // stringify/prettify/highlight with arguments: legacy's presentation
    // chain does not match, and the call is "unknown value or malformed
    // expression" (Q12) - also refused by W1.
    const bool presentation = c.id == BuiltinId::Stringify || c.id == BuiltinId::Prettify || c.id == BuiltinId::Highlight;
    // copy/deepcopy take their whole argument text as one expression (a
    // second argument is an expression error, not an arity message).
    const bool copy = c.id == BuiltinId::FnCopy || c.id == BuiltinId::FnDeepcopy;
    return ((listing || presentation) && c.args.size() != 1) || (copy && c.args.size() != 1);
}

std::string native_outcome(const Case& c) {
    Value out;
    std::string error;
    if (c.id == BuiltinId::Count) {
        const Status parsed_status = json_parse(Context{}, c.json, out, error);
        return parsed_status == Status::Ok ? builtins_test::shown(out) : "json-error " + error;
    }
    const Status status = call_builtin(builtin_spec(c.id), Context{}, c.args.data(),
                                       static_cast<std::uint32_t>(c.args.size()), out, error);
    if (status == Status::Ok && out.is(Kind::Bytes)) {
        const auto& data = bytes_of(out);
        return "ok bytes | " + nift::crypto::sha256_hex(nift::crypto::sha256_bytes(data.data(), data.size()));
    }
    if (status == Status::Ok) return builtins_test::shown(out);
    if (status == Status::Error) return "error " + error;
    // Review R4: a recoverable error is legacy's catchable Error; the program
    // wraps validate in try/catch, so legacy shows "caught <code> | <message>".
    if (status == Status::Recoverable)
        return "caught " + std::string(recoverable_code(c.id)) + " | " + error;
    if (status == Status::Limit) return "limit " + error;
    return "native-status " + std::to_string(static_cast<int>(status)) + " " + error;
}

std::string program_lines(const Case& c, std::size_t n) {
    std::ostringstream p;
    p << "print(\"@" << n << "\")\n";
    if (c.id == BuiltinId::Count) {  // a structured literal: legacy's strict JSON fast path
        p << "r" << n << " := " << c.json << "\nprint(type(r" << n << "))\nprint([r" << n << "].stringify())\n";
        return p.str();
    }
    const BuiltinSpec& spec = builtin_spec(c.id);
    for (std::size_t k = 0; k < c.args.size(); ++k) p << "c" << n << "_" << k << " := " << builtins_test::literal(c.args[k]) << "\n";
    std::string call;
    std::size_t first = 0;
    if (spec.receiver != Receiver::None) { call = "c" + std::to_string(n) + "_0."; first = 1; }
    call += std::string(spec.name) + "(";
    for (std::size_t k = first; k < c.args.size(); ++k) call += (k > first ? ", c" : "c") + std::to_string(n) + "_" + std::to_string(k);
    call += ")";
    if (c.id == BuiltinId::FnValidate) {
        // try/catch tells a catchable (recoverable) error from a fatal one.
        p << "try {\n  r" << n << " := " << call << "\n  print(type(r" << n << "))\n  print([r" << n
          << "].stringify())\n} catch(e" << n << ") {\n  print(\"caught \" + e" << n << ".code)\n  print(e" << n
          << ".message)\n}\n";
        return p.str();
    }
    p << "r" << n << " := " << call << "\nprint(type(r" << n << "))\n";
    // Bytes cannot be stringified: their SHA-256 identifies the content.
    if (c.expected.rfind("ok bytes", 0) == 0) p << "print(sha256(r" << n << "))\n";
    else p << "print([r" << n << "].stringify())\n";
    return p.str();
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// "error: <path>:<line>:<col>: <message>" -> "error <message>".
std::string legacy_error(const std::string& stderr_text) {
    std::string line = stderr_text.substr(0, stderr_text.find('\n'));
    std::size_t at = line.find(".f:");
    if (line.rfind("error: ", 0) != 0 || at == std::string::npos) return "error-unparsed " + line;
    at += 3;
    for (int field = 0; field < 2; ++field) {
        while (at < line.size() && std::isdigit(static_cast<unsigned char>(line[at]))) ++at;
        ++at;  // ':'
    }
    return "error " + line.substr(at + 1);
}

// Runs cases[from, to) in one legacy program; returns the index after the
// last case that got its legacy outcome.
std::size_t run_batch(const std::string& nift, const std::string& dir, std::vector<Case>& cases, std::size_t from,
                      std::size_t to) {
    {
        std::ofstream program(dir + "/p.f", std::ios::binary);
        for (std::size_t n = from; n < to; ++n) program << program_lines(cases[n], n);
    }
    const std::string command = "cd '" + dir + "' && env -u NIFT_VM -u NIFT_VM_STATS '" + nift +
                                "' p.f > out.txt 2> err.txt < /dev/null";
    const int status = std::system(command.c_str());
    std::istringstream out(slurp(dir + "/out.txt"));
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(out, line)) lines.push_back(line);
    std::size_t n = from, k = 0;
    while (k < lines.size() && n < to) {
        if (lines[k] != "@" + std::to_string(n)) break;
        if (k + 2 >= lines.size() || lines[k + 1].rfind('@', 0) == 0) break;
        cases[n].legacy = (lines[k + 1].rfind("caught ", 0) == 0 ? "" : "ok ") + lines[k + 1] + " | " + lines[k + 2];
        if (cases[n].expected.rfind("ok bytes", 0) == 0 && lines[k + 1] == "bytes") cases[n].legacy = "ok bytes | " + lines[k + 2];
        k += 3;
        ++n;
    }
    if (n == to) return to;
    // Case n started (its marker printed) and failed, or the program failed.
    cases[n].legacy = status == 0 ? "missing-output" : legacy_error(slurp(dir + "/err.txt"));
    return n + 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: builtins-diff <legacy nift> [seed] [cases]\n");
        return 2;
    }
    const std::string nift = argv[1];
    const unsigned long long seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 20261018ull;
    const std::size_t count = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 3000;
    rng.seed(seed);
    char dir_template[] = "/tmp/nift-cp14-diff-XXXXXX";
    const char* dir = mkdtemp(dir_template);
    if (!dir) { std::perror("mkdtemp"); return 2; }

    std::vector<Case> cases;
    for (std::size_t n = 0; n < count; ++n) {
        // One case in eight is a json_parse case (CP15).
        Case c{n % 8 == 7 ? BuiltinId::Count : static_cast<BuiltinId>((n - n / 8) % builtin_count()), {}, false, {}, {}, {}};
        if (c.id == BuiltinId::Count) {
            c.json = random_json(0, true);
            if (chance(30)) { c.json = corrupt_json(c.json); c.isolated = true; }
        }
        else c.args = arguments(c.id);
        c.expected = native_outcome(c);
        cases.push_back(static_cast<Case&&>(c));
    }
    constexpr std::size_t kBatch = 150;
    std::size_t runs = 0;
    for (std::size_t at = 0; at < cases.size(); ++runs) {
        std::size_t to = at + 1;
        if (!cases[at].isolated)
            while (to < cases.size() && to < at + kBatch && !cases[to].isolated) ++to;
        at = run_batch(nift, dir, cases, at, to);
    }

    std::map<std::string, std::pair<std::size_t, std::size_t>> per;  // name -> (cases, errors)
    std::size_t errors = 0, refused_cases = 0, invalid_legacy_ok = 0, invalid_legacy_error = 0;
    const char* show = std::getenv("BUILTINS_DIFF_SHOW");  // print the first N cases (both outcomes)
    const std::size_t shown_cases = show ? std::strtoull(show, nullptr, 10) : 0;
    for (std::size_t n = 0; n < cases.size(); ++n) {
        const Case& c = cases[n];
        if (n < shown_cases)
            std::printf("%s  native: %s\n  legacy: %s\n", program_lines(c, n).c_str(), c.expected.c_str(), c.legacy.c_str());
        static const char* const kPrefix[] = {"fn.", "str.", "arr.", "obj.", "num.", "bool.", "null.", "any.", "bytes."};
        auto& row = per[c.id == BuiltinId::Count ? std::string("json.parse (literal)")
                                                 : kPrefix[static_cast<int>(builtin_spec(c.id).receiver)] +
                                                       std::string(builtin_spec(c.id).name)];
        if (refused(c)) { ++refused_cases; continue; }
        if (c.id == BuiltinId::Count && c.expected.rfind("json-error ", 0) == 0) {
            // W9: not JSON, so the literal is read as Nift (legacy's fallback
            // evaluator, and the VM's CP14 literal path): tally whether it fails.
            ++(c.legacy.rfind("ok ", 0) == 0 ? invalid_legacy_ok : invalid_legacy_error);
            CHECK(c.expected.size() > 11);  // a parser message
            continue;
        }
        ++row.first;
        if (c.legacy.rfind("error", 0) == 0) { ++row.second; ++errors; }
        CHECK(c.expected == c.legacy);
        if (c.expected != c.legacy && rt_test::failures() <= 40) {
            std::fprintf(stderr, "case %zu: %s\n  native: %s\n  legacy: %s\n", n, program_lines(c, n).c_str(),
                         c.expected.c_str(), c.legacy.c_str());
        }
    }
    std::printf("builtins-diff: seed %llu, %zu cases (%zu compared, %zu legacy errors, %zu refused by rule W1) in %zu "
                "legacy runs\n", seed, cases.size(), cases.size() - refused_cases, errors, refused_cases, runs);
    std::printf("  invalid JSON literals (read as Nift, W9): %zu; legacy fallback errors %zu, legacy fallback "
                "succeeds %zu\n", invalid_legacy_ok + invalid_legacy_error, invalid_legacy_error, invalid_legacy_ok);
    for (const auto& row : per) std::printf("  %-20s %4zu cases, %4zu errors\n", row.first.c_str(), row.second.first, row.second.second);
    if (std::system(("rm -rf '" + std::string(dir) + "'").c_str()) != 0) std::fprintf(stderr, "cleanup failed\n");
    return rt_test::finish("builtins-diff");
}
