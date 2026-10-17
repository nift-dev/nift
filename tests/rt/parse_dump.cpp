// CP10 inspection and test driver for the Runtime 2.0 script parser (rt/).
//
//   parse_dump dump FILE              AST dump (or the diagnostic)
//   parse_dump check FILE...          one verdict line per file
//   parse_dump reprint FILE           source with explicit parentheses around
//                                     every structured operator expression
//   parse_dump errors CORPUS          verify a stored error corpus
//   parse_dump fuzz SEED ITERS FILE...  random byte/structure mutations
//   parse_dump bench ITERS FILE...    parse repeatedly (callgrind)
//   parse_dump strings FILE           rt's reading of each string literal:
//                                     escapes decoded, $[...] segments as
//                                     \x01text\x02 (negative gate)
#include "rt_parser_tool.h"

#include <algorithm>
#include <chrono>
#include <iostream>

using namespace nift::rt;

namespace {

int usage() {
    std::cerr << "usage: parse_dump dump|check|reprint|errors|fuzz|bench ...\n";
    return 2;
}

bool load(const std::string& path, std::string& source) {
    std::string raw;
    if (!rt_tool::read_file(path, raw)) {
        std::cerr << "cannot read " << path << '\n';
        return false;
    }
    source = rt_tool::strip_shebang(raw);
    return true;
}

int dump_mode(const std::string& path) {
    std::string source;
    if (!load(path, source)) return 2;
    Ast ast(source);
    const auto result = parse_script(ast);
    if (!result.ok) {
        const auto where = locate(source, result.error.offset);
        std::cout << "error: " << path << ':' << where.line << ':' << where.column << ": " << result.error.message
                  << " [" << ParseError::kCode << "]\n";
        return 1;
    }
    std::cout << dump(ast, result.root);
    return 0;
}

int check_mode(int argc, char** argv) {
    std::map<std::string, std::size_t> total;
    std::size_t files = 0, accepted = 0;
    for (int i = 2; i < argc; ++i) {
        std::string source;
        if (!load(argv[i], source)) return 2;
        std::map<std::string, std::size_t> reasons;
        const auto verdict = rt_tool::rt_verdict(source, &reasons);
        ++files;
        accepted += verdict.ok;
        std::cout << argv[i] << '\t' << verdict.str();
        for (const auto& [reason, count] : reasons) {
            std::cout << '\t' << reason << '=' << count;
            total[reason] += count;
        }
        std::cout << '\n';
    }
    std::cerr << "files=" << files << " accepted=" << accepted;
    for (const auto& [reason, count] : total) std::cerr << ' ' << reason << '=' << count;
    std::cerr << '\n';
    return 0;
}

// Parenthesised reprint: every unary/binary/logical/coalesce/ternary node is
// wrapped in parentheses, and so is any postfix expression that is the
// operand of one (string literals are left as they are). Positions of all
// other text are unchanged, so a legacy run
// of the reprint must behave exactly like a run of the original if (and only
// if) this parser's precedence matches the legacy evaluator's.
struct Insert {
    std::uint32_t at;
    bool open;
    std::uint32_t other;  // the far end of the node, for ordering
};

// Await is wrapped too: legacy's await takes the whole rest of its unit, so a
// misplaced await boundary shows up as a difference.
bool wraps(NodeKind kind) {
    return kind == NodeKind::Unary || kind == NodeKind::Binary || kind == NodeKind::Logical ||
           kind == NodeKind::Coalesce || kind == NodeKind::Ternary || kind == NodeKind::Await;
}

bool postfix(NodeKind kind) {
    return kind == NodeKind::Call || kind == NodeKind::Member || kind == NodeKind::Index ||
           kind == NodeKind::SafeMember || kind == NodeKind::SafeIndex;
}

void collect(const Ast& ast, NodeId root, std::vector<Insert>& out) {
    std::vector<std::pair<NodeId, bool>> stack{{root, false}};  // (node, is an operator operand)
    while (!stack.empty()) {
        const auto [id, operand] = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& node = ast.at(id);
        // Opaque text is legacy's; string contents are observable as literal
        // text where legacy does not interpolate (the loop path, semantics.md),
        // so neither is touched.
        if (node.kind == NodeKind::Opaque || node.kind == NodeKind::String) continue;
        if (wraps(node.kind) || (operand && postfix(node.kind))) {
            out.push_back({node.begin, true, node.end});
            out.push_back({node.end, false, node.begin});
        }
        // An await operand is left alone: `await (f())` is the removed
        // `await(...)` form in some legacy contexts (while conditions).
        const bool child_operand = wraps(node.kind) && node.kind != NodeKind::Await;
        for (NodeId child : {node.a, node.b, node.c}) stack.push_back({child, child_operand});
        for (std::uint32_t i = 0; i < node.count; ++i) stack.push_back({ast.list(node)[i], false});
    }
}

std::string reprint(const Ast& ast, NodeId root) {
    std::vector<Insert> inserts;
    collect(ast, root, inserts);
    // At one offset: closes before opens; outer opens first; inner closes first.
    std::stable_sort(inserts.begin(), inserts.end(), [](const Insert& x, const Insert& y) {
        if (x.at != y.at) return x.at < y.at;
        if (x.open != y.open) return !x.open;
        return x.open ? x.other > y.other : x.other > y.other;
    });
    std::string out;
    const std::string_view src = ast.source();
    std::uint32_t p = 0;
    for (const auto& insert : inserts) {
        out.append(src.substr(p, insert.at - p));
        out += insert.open ? '(' : ')';
        p = insert.at;
    }
    out.append(src.substr(p));
    return out;
}

int reprint_mode(const std::string& path) {
    std::string raw;
    if (!rt_tool::read_file(path, raw)) return 2;
    const std::string source = rt_tool::strip_shebang(raw);
    const std::string header = raw.substr(0, raw.size() - source.size());
    Ast ast(source);
    const auto result = parse_script(ast);
    if (!result.ok) return 1;
    std::cout << header << reprint(ast, result.root);
    return 0;
}

int errors_mode(const std::string& path) {
    std::string text;
    std::vector<rt_tool::ErrorCase> cases;
    if (!rt_tool::read_file(path, text) || !rt_tool::read_cases(text, cases)) {
        std::cerr << "cannot read error corpus " << path << '\n';
        return 2;
    }
    std::size_t matched = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto got = rt_tool::rt_verdict(cases[i].source);
        if (got.str() == cases[i].expected.str()) { ++matched; continue; }
        std::cout << "MISMATCH case " << i << "\n  legacy: " << cases[i].expected.str() << "\n  rt:     " << got.str()
                  << '\n';
    }
    std::cout << "error corpus: " << matched << '/' << cases.size() << " identical\n";
    return matched == cases.size() && !cases.empty() ? 0 : 1;
}

int fuzz_mode(int argc, char** argv) {
    if (argc < 5) return usage();
    rt_tool::Mutator mutator(std::stoull(argv[2]));
    const long iterations = std::stol(argv[3]);
    std::vector<std::string> seeds;
    for (int i = 4; i < argc; ++i) {
        std::string source;
        if (!load(argv[i], source)) return 2;
        seeds.push_back(source);
    }
    std::size_t accepted = 0, rejected = 0;
    double worst_ms = 0;
    for (long n = 0; n < iterations; ++n) {
        const std::string& seed = seeds[mutator.below(seeds.size())];
        const std::string input = (n % 2) ? mutator.bytes(seed) : mutator.structural(seed);
        const auto start = std::chrono::steady_clock::now();
        const auto verdict = rt_tool::rt_verdict(input);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        worst_ms = std::max(worst_ms, ms);
        (verdict.ok ? accepted : rejected) += 1;
    }
    // Deep nesting: bounded recursion, no crash.
    for (std::size_t depth : {100u, 1000u, 10000u}) {
        rt_tool::rt_verdict("x := " + std::string(depth, '(') + "1" + std::string(depth, ')'));
        rt_tool::rt_verdict("x := " + std::string(depth, '[') + std::string(depth, ']'));
        rt_tool::rt_verdict("x := " + std::string(depth, '-') + "1");
        std::string sum = "x := 1";
        for (std::size_t i = 0; i < depth; ++i) sum += " + 1";
        rt_tool::rt_verdict(sum);
    }
    std::cout << "fuzz: iterations=" << iterations << " accepted=" << accepted << " rejected=" << rejected
              << " worst_ms=" << worst_ms << '\n';
    return 0;
}

// Legacy scalar_literal escape decoding (ParserExpression.cpp:458): \n \r \t
// \\ \" \' map to their characters, any other escaped byte to itself.
std::string decode_escapes(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) { out += text[i]; continue; }
        const char c = text[++i];
        out += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c;
    }
    return out;
}

std::string escaped(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c < 0x20 || c >= 0x7f) {
            static const char hex[] = "0123456789abcdef";
            out += "\\x";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else out += static_cast<char>(c);
    }
    return out;
}

int strings_mode(const std::string& path) {
    std::string source;
    if (!load(path, source)) return 2;
    Ast ast(source);
    const auto result = parse_script(ast);
    if (!result.ok) return 1;
    std::vector<NodeId> stack{result.root};
    std::vector<NodeId> strings;
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& node = ast.at(id);
        if (node.kind == NodeKind::String) { strings.push_back(id); continue; }
        for (std::uint32_t i = node.count; i-- > 0;) stack.push_back(ast.list(node)[i]);
        for (NodeId child : {node.c, node.b, node.a}) stack.push_back(child);
    }
    for (NodeId id : strings) {
        const Node& node = ast.at(id);
        std::string text;
        if (node.count == 0) {
            text = decode_escapes(ast.text(node).substr(1, node.end - node.begin - 2));
        } else {
            for (std::uint32_t i = 0; i < node.count; ++i) {
                const Node& part = ast.at(ast.list(node)[i]);
                if (part.kind == NodeKind::StringPart) text += decode_escapes(ast.text(part));
                else text += "\x01" + std::string(ast.text(part)) + "\x02";
            }
        }
        std::cout << "S\t" << escaped(text) << '\n';
    }
    return 0;
}

int bench_mode(int argc, char** argv) {
    if (argc < 4) return usage();
    const long iterations = std::stol(argv[2]);
    std::vector<std::string> sources;
    std::size_t bytes = 0;
    for (int i = 3; i < argc; ++i) {
        std::string source;
        if (!load(argv[i], source)) return 2;
        bytes += source.size();
        sources.push_back(std::move(source));
    }
    std::size_t nodes = 0;
    for (long n = 0; n < iterations; ++n) {
        for (const auto& source : sources) {
            Ast ast(source);
            parse_script(ast);
            nodes += ast.size();
        }
    }
    std::cout << "bench: files=" << sources.size() << " bytes=" << bytes << " iterations=" << iterations
              << " nodes=" << nodes << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string mode = argv[1];
    if (mode == "dump") return dump_mode(argv[2]);
    if (mode == "check") return check_mode(argc, argv);
    if (mode == "reprint") return reprint_mode(argv[2]);
    if (mode == "errors") return errors_mode(argv[2]);
    if (mode == "fuzz") return fuzz_mode(argc, argv);
    if (mode == "bench") return bench_mode(argc, argv);
    if (mode == "strings") return strings_mode(argv[2]);
    return usage();
}
