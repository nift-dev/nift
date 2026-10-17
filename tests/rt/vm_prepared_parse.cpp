// CP11 review round 2 (M2): rt::legacy_prepared_parse_accepts must accept
// exactly the texts legacy's prepared parser (nift::ast::parse_expression,
// src/Ast.cpp, linked here) accepts. Differential over hand-written cases and
// a deterministic random corpus of expression-shaped texts.
//
//   vm_prepared_parse [count] [seed]
#include "Ast.h"
#include "rt/prepared_parse.h"
#include "rt_test.h"

#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

int differences = 0;

void compare(const std::string& text) {
    const bool legacy = nift::ast::parse_expression(text).supported;
    const bool ported = nift::rt::legacy_prepared_parse_accepts(text);
    CHECK(legacy == ported);
    if (legacy != ported && ++differences <= 20)
        std::fprintf(stderr, "  [%s]: legacy %d, port %d\n", text.c_str(), legacy, ported);
}

const std::vector<std::string> kCases = {
    "", " ", "x", "(x)", "((x))", "(true ? p : 0)", "p < 2 == true", "1 < 2", "a == b == c", "a != (b == c)",
    "(a < b) == c", "p ?? 0", "(p ?? 0)", "a ?? b ?? c", "a || b ?? c", "!a == b", "!!x", "-x * 2", "+-x",
    "[1, 2]", "[1, 2,]", "[]", "[ ]", "{}", "{\"k\": 1}", "{\"k\": \"v\"}", "{\"k\": 'v'}", "{k: 1}",
    "{\"a\\tb\": 1}", "{\"k\": [1, {\"j\": 2}]}", "{\"k\": 1,}", "\"s\"", "'s'", "\"a\\\"b\"", "\"unterminated",
    "1", "1.5", ".5", "1.", "1e5", "1e400", "0x10", "9007199254740992", "9007199254740993", "-9007199254740993",
    "99999999999999999999", "007", "1.5.2", "2abc", "f(1)", "f (1)", "f(1, (2 ? 3 : 4))", "f((c ? x : y))",
    "f(", "f(1))", "o.k", "o . k", "o.1", "o?.k", "o?[1]", "a[0]", "a[0", "a[(1 ? 2 : 3)]", "s.length()",
    "x.f(1)(2)", "(x) => x + 1", "(x)=>x", "x => x", "(\"x)=>\")", "a + b * c - d / e % f", "a && b || c && d",
    "a <= b", "a >= b", "a<b", "a < b < c", "1 +", "* 2", "a\fb", "a +\f1", "\v1", "x\n+ 1", "x\r\n+\t1",
    "true", "false", "null", "null.k", "[1][0]", "\"abc\"[1]", "(1)(2)", "a ? b", "a : b", "#", "$x", "@x",
    "a & b", "a | b", "a ^ b", "a === b", "a !== b", "a =< b", "a => b", "x = 1", "x += 1", "x++", "++x",
};

std::string random_expr(std::mt19937& rng, int depth) {
    auto pick = [&](const std::vector<std::string>& items) { return items[rng() % items.size()]; };
    static const std::vector<std::string> atoms = {
        "x", "p", "_q1", "true", "null", "1", "2.5", ".5", "1.", "1e3", "0x1F", "9007199254740993", "\"s\"",
        "'t'", "\"a\\\"b\"", "[]", "{}", "[1, 2]", "{\"k\": 1}", "{\"k\": \"v\"}", "f()", "g(1, 2)", "o.k", "a[0]",
        "s.length()", "(x)", "\"(\"", "\")\"", "\"=>\""};
    static const std::vector<std::string> ops = {
        " + ", " - ", " * ", " / ", " % ", " == ", " != ", " < ", " <= ", " > ", " >= ", " && ", " || ", " ?? ",
        "+", "-", "==", "<", "&&", "??", " ? ", " : ", " = ", " & ", "\f+\f", "\v<", " => "};
    static const std::vector<std::string> prefixes = {"!", "-", "+", "!!", "- ", "! "};
    if (depth > 4) return pick(atoms);
    switch (rng() % 9) {
        case 0: case 1: return pick(atoms);
        case 2: case 3: return random_expr(rng, depth + 1) + pick(ops) + random_expr(rng, depth + 1);
        case 4: return pick(prefixes) + random_expr(rng, depth + 1);
        case 5: return "(" + random_expr(rng, depth + 1) + ")";
        case 6: return "[" + random_expr(rng, depth + 1) + ", " + random_expr(rng, depth + 1) + "]";
        case 7: return "f(" + random_expr(rng, depth + 1) + ", " + random_expr(rng, depth + 1) + ")";
        default: return "{\"k\": " + random_expr(rng, depth + 1) + "}";
    }
}

std::string mutate(std::mt19937& rng, std::string text) {
    static const std::string bytes = "()[]{}\"',:?!.=<>+-*/%&|^ \t\n\f\v\\$@#0x1e";
    const int edits = static_cast<int>(rng() % 3);
    for (int k = 0; k < edits && !text.empty(); ++k) {
        const std::size_t at = rng() % text.size();
        switch (rng() % 3) {
            case 0: text.erase(at, 1); break;
            case 1: text.insert(at, 1, bytes[rng() % bytes.size()]); break;
            default: text[at] = bytes[rng() % bytes.size()]; break;
        }
    }
    return text;
}

} // namespace

int main(int argc, char** argv) {
    const long count = argc > 1 ? std::strtol(argv[1], nullptr, 10) : 50000;
    const unsigned seed = argc > 2 ? static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10)) : 20261018u;
    for (const auto& text : kCases) compare(text);
    // Limits: recursion depth 128, bracket depth 256, size 8,192.
    for (int depth : {60, 63, 64, 65, 127, 128, 129, 200, 255, 256, 257, 300})
        compare(std::string(static_cast<std::size_t>(depth), '(') + "1" + std::string(static_cast<std::size_t>(depth), ')'));
    for (int depth : {60, 63, 64, 65, 127, 128, 129, 200})
        compare(std::string(static_cast<std::size_t>(depth), '!') + "x");
    for (int depth : {100, 128, 129, 255, 256, 257})
        compare(std::string(static_cast<std::size_t>(depth), '[') + "1" + std::string(static_cast<std::size_t>(depth), ']'));
    for (std::size_t size : {8190u, 8191u, 8192u, 8193u, 9000u}) compare("\"" + std::string(size - 2, 'a') + "\"");
    std::mt19937 rng(seed);
    for (long k = 0; k < count; ++k) {
        const std::string text = random_expr(rng, 0);
        compare(text);
        compare(mutate(rng, text));
    }
    return rt_test::finish("rt prepared-parser port (differential vs legacy)");
}
