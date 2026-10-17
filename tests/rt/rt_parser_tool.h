#pragma once
// Shared helpers for the CP10 parser test binaries (parse_dump, legacy_oracle).
#include "rt/ast.h"
#include "rt/parser.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace rt_tool {

inline bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

// The CLI drops a leading `#!` line before translation (src/CLI.cpp
// run_script_file); diagnostics are relative to the remaining text.
inline std::string strip_shebang(const std::string& source) {
    if (source.rfind("#!", 0) != 0) return source;
    const auto nl = source.find('\n');
    return nl == std::string::npos ? std::string() : source.substr(nl + 1);
}

struct Verdict {
    bool ok = true;
    std::size_t line = 0, column = 0;
    std::string message;
    std::string str() const {
        if (ok) return "ok";
        return std::to_string(line) + ":" + std::to_string(column) + ": " + message;
    }
};

// Opaque nodes reachable from `id` (the arena also holds nodes of abandoned
// parse attempts, which are not part of the tree).
inline void count_opaque(const nift::rt::Ast& ast, nift::rt::NodeId root, std::map<std::string, std::size_t>& out) {
    std::vector<nift::rt::NodeId> stack{root};  // iterative: no recursion on deep trees
    while (!stack.empty()) {
        const nift::rt::NodeId id = stack.back();
        stack.pop_back();
        if (id == nift::rt::kNoNode) continue;
        const auto& node = ast.at(id);
        if (node.kind == nift::rt::NodeKind::Opaque) ++out[nift::rt::opaque_reason_name(node.reason)];
        stack.insert(stack.end(), {node.a, node.b, node.c});
        for (std::uint32_t i = 0; i < node.count; ++i) stack.push_back(ast.list(node)[i]);
    }
}

inline Verdict rt_verdict(const std::string& source, std::map<std::string, std::size_t>* reasons = nullptr) {
    nift::rt::Ast ast(source);
    const auto result = nift::rt::parse_script(ast);
    Verdict verdict;
    verdict.ok = result.ok;
    if (!result.ok) {
        const auto where = nift::rt::locate(source, result.error.offset);
        verdict.line = where.line;
        verdict.column = where.column;
        verdict.message = result.error.message;
    } else if (reasons) {
        count_opaque(ast, result.root, *reasons);
    }
    return verdict;
}

// Error-corpus record: "#case <bytes> <line> <col>\n<message>\n<source>\n".
struct ErrorCase {
    std::string source;
    Verdict expected;
};

inline void write_case(std::ostream& out, const ErrorCase& c) {
    out << "#case " << c.source.size() << ' ' << c.expected.line << ' ' << c.expected.column << '\n'
        << c.expected.message << '\n' << c.source << '\n';
}

inline bool read_cases(const std::string& text, std::vector<ErrorCase>& cases) {
    std::size_t p = 0;
    while (p < text.size()) {
        const auto header_end = text.find('\n', p);
        if (header_end == std::string::npos) return false;
        std::istringstream header(text.substr(p, header_end - p));
        std::string tag;
        std::size_t bytes = 0;
        ErrorCase c;
        c.expected.ok = false;
        if (!(header >> tag >> bytes >> c.expected.line >> c.expected.column) || tag != "#case") return false;
        const auto message_end = text.find('\n', header_end + 1);
        if (message_end == std::string::npos) return false;
        c.expected.message = text.substr(header_end + 1, message_end - header_end - 1);
        if (message_end + 1 + bytes + 1 > text.size()) return false;
        c.source = text.substr(message_end + 1, bytes);
        cases.push_back(std::move(c));
        p = message_end + 1 + bytes + 1;
    }
    return true;
}

// Deterministic structural mutations used for the error corpus and fuzzing.
class Mutator {
public:
    explicit Mutator(std::uint64_t seed) : state_(seed * 2862933555777941757ULL + 3037000493ULL) {}
    std::uint64_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return state_;
    }
    std::size_t below(std::size_t n) { return n ? static_cast<std::size_t>(next() % n) : 0; }

    std::string structural(const std::string& in) {
        static const char* const inserts[] = {
            "(", ")", "{", "}", "[", "]", "\"", "'", ";", "\n", "/*", "@/*", "if ", "if(", "else ",
            "else if ", "fn ", "fn(", "while ", "while(", "for ", "for(", "try ", "try {", "catch",
            "catch(", "throw ", "throw\n", "struct ", "struct(", "enum ", "enum {", "export ", "export(",
            "import(", "return ", "break", "continue", "=>", ":=", "`", "<!--", "@//", "//", "\\", ",", "$["};
        std::string out = in;
        const int edits = 1 + static_cast<int>(below(3));
        for (int e = 0; e < edits; ++e) {
            const std::size_t at = below(out.size() + 1);
            switch (below(5)) {
            case 0: out.erase(at, 1 + below(3)); break;
            case 1: out.insert(at, inserts[below(sizeof(inserts) / sizeof(*inserts))]); break;
            case 2: {
                const auto close = out.find_first_of(")}]", at);
                if (close != std::string::npos) out.erase(close, 1);
                break;
            }
            case 3: {
                const auto open = out.find_first_of("({[\"'", at);
                if (open != std::string::npos) out.insert(open, 1, out[open]);
                break;
            }
            default: {
                const auto nl = out.find('\n', at);
                if (nl != std::string::npos) out.erase(nl, 1);
                break;
            }
            }
        }
        return out;
    }

    std::string bytes(const std::string& in) {
        std::string out = in;
        const int edits = 1 + static_cast<int>(below(8));
        for (int e = 0; e < edits; ++e) {
            const std::size_t at = below(out.size() + 1);
            const char byte = static_cast<char>(below(256));
            switch (below(3)) {
            case 0: if (at < out.size()) out[at] = byte; break;
            case 1: out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), byte); break;
            default: out.erase(at, 1 + below(4)); break;
            }
        }
        return out;
    }

private:
    std::uint64_t state_;
};

} // namespace rt_tool
