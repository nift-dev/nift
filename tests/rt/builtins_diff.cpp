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

Value random_number() {
    switch (pick(6)) {
        case 0: return Value::number(kDoubles[pick(sizeof kDoubles / sizeof kDoubles[0])]);
        case 1: return Value::integer(9007199254740993 + static_cast<std::int64_t>(pick(3)));
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

struct Case {
    BuiltinId id;
    std::vector<Value> args;
    std::string expected;  // the native's outcome
    std::string legacy;    // the legacy outcome
};

// Calls the compiler refuses before execution (wiring rule W1), which the
// differential test therefore does not compare: keys/values/entries with
// arguments are an arity error on most receivers but ignore the arguments on
// a binding (shape-dependent legacy behaviour, design record Q10).
bool refused(const Case& c) {
    const bool listing = c.id == BuiltinId::ObjKeys || c.id == BuiltinId::ObjValues || c.id == BuiltinId::ObjEntries;
    return listing && c.args.size() != 1;
}

std::string native_outcome(const Case& c) {
    Value out;
    std::string error;
    const Status status = call_builtin(builtin_spec(c.id), Context{}, c.args.data(),
                                       static_cast<std::uint32_t>(c.args.size()), out, error);
    if (status == Status::Ok) return builtins_test::shown(out);
    if (status == Status::Error) return "error " + error;
    return "native-status " + std::to_string(static_cast<int>(status)) + " " + error;
}

std::string program_lines(const Case& c, std::size_t n) {
    const BuiltinSpec& spec = builtin_spec(c.id);
    std::ostringstream p;
    p << "print(\"@" << n << "\")\n";
    for (std::size_t k = 0; k < c.args.size(); ++k) p << "c" << n << "_" << k << " := " << builtins_test::literal(c.args[k]) << "\n";
    std::string call;
    std::size_t first = 0;
    if (spec.receiver != Receiver::None) { call = "c" + std::to_string(n) + "_0."; first = 1; }
    call += std::string(spec.name) + "(";
    for (std::size_t k = first; k < c.args.size(); ++k) call += (k > first ? ", c" : "c") + std::to_string(n) + "_" + std::to_string(k);
    call += ")";
    p << "r" << n << " := " << call << "\nprint(type(r" << n << "))\nprint([r" << n << "].stringify())\n";
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
        cases[n].legacy = "ok " + lines[k + 1] + " | " + lines[k + 2];
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
        Case c{static_cast<BuiltinId>(n % builtin_count()), {}, {}, {}};
        c.args = arguments(c.id);
        c.expected = native_outcome(c);
        cases.push_back(static_cast<Case&&>(c));
    }
    constexpr std::size_t kBatch = 150;
    std::size_t runs = 0;
    for (std::size_t at = 0; at < cases.size(); ++runs) at = run_batch(nift, dir, cases, at, std::min(at + kBatch, cases.size()));

    std::map<std::string, std::pair<std::size_t, std::size_t>> per;  // name -> (cases, errors)
    std::size_t errors = 0, refused_cases = 0;
    const char* show = std::getenv("BUILTINS_DIFF_SHOW");  // print the first N cases (both outcomes)
    const std::size_t shown_cases = show ? std::strtoull(show, nullptr, 10) : 0;
    for (std::size_t n = 0; n < cases.size(); ++n) {
        const Case& c = cases[n];
        if (n < shown_cases)
            std::printf("%s  native: %s\n  legacy: %s\n", program_lines(c, n).c_str(), c.expected.c_str(), c.legacy.c_str());
        const BuiltinSpec& spec = builtin_spec(c.id);
        auto& row = per[std::string(spec.receiver == Receiver::String ? "str." : spec.receiver == Receiver::Array ? "arr."
                                    : spec.receiver == Receiver::Object ? "obj." : "fn.") + std::string(spec.name)];
        if (refused(c)) { ++refused_cases; continue; }
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
    for (const auto& row : per) std::printf("  %-20s %4zu cases, %4zu errors\n", row.first.c_str(), row.second.first, row.second.second);
    if (std::system(("rm -rf '" + std::string(dir) + "'").c_str()) != 0) std::fprintf(stderr, "cleanup failed\n");
    return rt_test::finish("builtins-diff");
}
