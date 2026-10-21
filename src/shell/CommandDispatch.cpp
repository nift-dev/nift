#include "shell/CommandDispatch.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace nift::shell {
namespace {

const std::set<std::string>& nift_keywords() {
    static const std::set<std::string> keywords = {
        "fn", "if", "for", "while", "struct", "try", "return", "break",
        "continue", "import", "export", "throw", "else", "elif",
    };
    return keywords;
}

bool is_assignment_operator(const std::string& token) {
    return token == "=" || token == ":=" || token == "+=" || token == "-=" ||
           token == "*=" || token == "/=" || token == "%=";
}

bool is_env_assignment(const std::string& token) {
    const std::size_t eq = token.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    if (!(std::isalpha(static_cast<unsigned char>(token[0])) || token[0] == '_')) return false;
    for (std::size_t i = 0; i < eq; ++i) {
        const unsigned char c = static_cast<unsigned char>(token[i]);
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    return true;
}

bool is_redirection_operator(const std::string& token) {
    return token == "<" || token == ">" || token == ">>" || token == "2>" ||
           token == "2>>" || token == "2>&1";
}

bool is_connector_text(const std::string& token) {
    return token == "&&" || token == "||" || token == ";";
}

bool is_operator_text(const std::string& token) {
    return is_connector_text(token) || token == "|" || token == "&" ||
           is_redirection_operator(token);
}

bool is_path_prefix(const std::string& token) {
    if (token.empty()) return false;
    if (token[0] == '/') return true;
    if (token.rfind("./", 0) == 0 || token.rfind("../", 0) == 0) return true;
    if (token.rfind(".\\", 0) == 0 || token.rfind("..\\", 0) == 0) return true;
    if (token[0] == '\\') return true;
    if (token.size() >= 2 && std::isalpha(static_cast<unsigned char>(token[0])) &&
        token[1] == ':') return true;
    return token.find('/') != std::string::npos ||
           token.find('\\') != std::string::npos;
}

std::string trim_left(const std::string& value) {
    std::size_t i = 0;
    while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i]))) ++i;
    return value.substr(i);
}

std::size_t first_word_index(const std::vector<std::string>& tokens) {
    std::size_t i = 0;
    while (i < tokens.size() && is_env_assignment(tokens[i])) ++i;
    return i;
}

void fill_args(const std::vector<std::string>& tokens, std::size_t name_index, Decision& out) {
    out.args.assign(tokens.begin() + static_cast<std::ptrdiff_t>(name_index + 1), tokens.end());
}

}  // namespace

bool looks_like_path(const std::string& token) {
    return is_path_prefix(token);
}

bool is_nift_literal(const std::string& token) {
    if (token == "true" || token == "false" || token == "null") return true;
    if (token.empty()) return false;
    char* end = nullptr;
    const char* begin = token.c_str();
    std::strtod(begin, &end);
    return end && *end == '\0' && end != begin;
}

bool is_nift_statement_line(const std::vector<std::string>& tokens,
                            const std::string& raw_line,
                            const ShellSnapshot& snapshot) {
    if (tokens.empty()) return true;
    const std::string& first = tokens.front();
    if (first.empty()) return true;
    if (first == "command") return false;
    if (snapshot.shell_builtins.count(first)) return false;

    const std::string raw = trim_left(raw_line);
    if (!raw.empty() && (raw[0] == '"' || raw[0] == '\'')) return true;

    if (first[0] == '#') return true;
    if (first.find('(') != std::string::npos) return true;
    if (nift_keywords().count(first)) return true;
    if (first.find(":=") != std::string::npos) return true;
    if (tokens.size() >= 2 && is_assignment_operator(tokens[1])) return true;
    for (const auto& token : tokens) {
        if (token == "{" || token == "}") return true;
    }

    if (tokens.size() == 1) {
        if (first == "pwd") return false;  // command-form built-in
        if (snapshot.command_builtins.count(first)) return first == "print";
        if (is_nift_literal(first)) return true;
        if (snapshot.nift_expression_names.count(first)) return true;
        if (snapshot.user_functions.count(first)) return true;
        return false;  // PATH executable or command-not-found
    }
    return false;
}

Decision classify_segment(const std::vector<std::string>& tokens,
                          const ShellSnapshot& snapshot,
                          bool command_line) {
    Decision out;
    std::size_t index = first_word_index(tokens);
    if (index >= tokens.size()) {
        out.kind = CommandKind::NotFound;
        return out;
    }

    bool command_prefix = false;
    if (tokens[index] == "command") {
        command_prefix = true;
        ++index;
        if (index >= tokens.size()) {
            out.kind = CommandKind::NotFound;
            out.name = "command";
            out.command_prefix = true;
            return out;
        }
    }

    out.name = tokens[index];
    out.command_prefix = command_prefix;
    out.explicit_path = looks_like_path(out.name);
    fill_args(tokens, index, out);

    if (command_prefix) {
        out.kind = (out.explicit_path || (snapshot.executable_exists && snapshot.executable_exists(out.name)))
                       ? CommandKind::External
                       : CommandKind::NotFound;
        return out;
    }

    if (snapshot.shell_builtins.count(out.name)) {
        out.kind = CommandKind::ShellBuiltin;
        return out;
    }
    if (snapshot.user_functions.count(out.name)) {
        // Outside a command line a bare user name is the existing Nift
        // expression statement; inside one it is a shell-style function call
        // (step 3), including a zero-argument call.
        out.kind = (tokens.size() == 1 && !command_line) ? CommandKind::NiftStatement
                                                         : CommandKind::UserFunction;
        return out;
    }
    if (snapshot.command_builtins.count(out.name)) {
        out.kind = CommandKind::NiftBuiltin;
        return out;
    }
    if (!command_line && tokens.size() == 1 &&
        (is_nift_literal(out.name) || snapshot.nift_expression_names.count(out.name))) {
        out.kind = CommandKind::NiftStatement;
        return out;
    }
    if (out.explicit_path) {
        out.kind = (snapshot.executable_exists && snapshot.executable_exists(out.name))
                       ? CommandKind::External
                       : CommandKind::NotFound;
        return out;
    }
    if (out.name.find("$[") != std::string::npos) {
        // Interpolation may only change the program text; the mechanism is
        // already final and external. If the expanded name is not executable,
        // process launch reports it.
        out.kind = CommandKind::External;
        return out;
    }
    if (snapshot.executable_exists && snapshot.executable_exists(out.name)) {
        out.kind = CommandKind::External;
        return out;
    }
    out.kind = CommandKind::NotFound;
    return out;
}

LinePlan classify_line(const std::vector<std::string>& tokens,
                       const std::vector<bool>& operator_token,
                       const ShellSnapshot& snapshot) {
    LinePlan plan;
    std::size_t start = 0;
    std::string connector;
    for (std::size_t i = 0; i <= tokens.size(); ++i) {
        const bool at_end = i == tokens.size();
        const bool at_connector = !at_end && i < operator_token.size() && operator_token[i] &&
                                  is_connector_text(tokens[i]);
        if (!at_end && !at_connector) continue;
        if (i > start) {
            Segment segment;
            segment.begin = start;
            segment.end = i;
            segment.connector = connector;
            segment.tokens.assign(tokens.begin() + static_cast<std::ptrdiff_t>(start),
                                  tokens.begin() + static_cast<std::ptrdiff_t>(i));
            for (std::size_t j = start; j < i; ++j) {
                if (j < operator_token.size() && operator_token[j]) {
                    if (tokens[j] == "|") segment.has_pipe = true;
                    if (is_redirection_operator(tokens[j])) segment.has_redirection = true;
                }
            }
            segment.background = i > start && operator_token.size() >= i &&
                                 operator_token[i - 1] && tokens[i - 1] == "&";
            segment.first = classify_segment(segment.tokens, snapshot, /*command_line=*/true);
            plan.segments.push_back(std::move(segment));
        }
        if (at_connector) connector = tokens[i];
        start = i + 1;
    }
    return plan;
}

LinePlan classify_line(const std::vector<std::string>& tokens,
                       const ShellSnapshot& snapshot) {
    std::vector<bool> operator_token;
    operator_token.reserve(tokens.size());
    for (const auto& token : tokens) operator_token.push_back(is_operator_text(token));
    return classify_line(tokens, operator_token, snapshot);
}

const char* command_kind_name(CommandKind kind) {
    switch (kind) {
        case CommandKind::NiftStatement: return "NiftStatement";
        case CommandKind::ShellBuiltin: return "ShellBuiltin";
        case CommandKind::UserFunction: return "UserFunction";
        case CommandKind::NiftBuiltin: return "NiftBuiltin";
        case CommandKind::External: return "External";
        case CommandKind::NotFound: return "NotFound";
    }
    return "Unknown";
}

}  // namespace nift::shell
