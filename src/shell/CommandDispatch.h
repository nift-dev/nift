#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace nift::shell {

// D-042 command-form classification. The classifier is pure: it never runs
// Nift code, starts a process or touches the filesystem. The PATH predicate is
// a read-only callback supplied by the caller.
enum class CommandKind {
    NiftStatement,
    ShellBuiltin,
    UserFunction,
    NiftBuiltin,
    External,
    NotFound,
};

struct ShellSnapshot {
    std::set<std::string> shell_builtins;
    std::set<std::string> user_functions;
    // Bare-token Nift expressions (literals and non-command built-ins).
    std::set<std::string> nift_expression_names;
    // Command-form Nift built-ins: exactly print and pwd (D-042 P1-b).
    std::set<std::string> command_builtins;
    std::function<bool(const std::string&)> executable_exists;
    bool windows = false;
};

struct Decision {
    CommandKind kind = CommandKind::NotFound;
    std::string name;
    std::vector<std::string> args;
    bool explicit_path = false;
    bool command_prefix = false;
};

struct Segment {
    std::string connector;  // connector before this segment: "", "&&", "||", ";"
    std::vector<std::string> tokens;
    Decision first;
    bool has_pipe = false;
    bool has_redirection = false;
    bool background = false;
    std::size_t begin = 0;  // [begin, end) in the original token vector
    std::size_t end = 0;
};

struct LinePlan {
    bool nift_statement = false;
    std::vector<Segment> segments;
};

bool looks_like_path(const std::string& token);
bool is_nift_literal(const std::string& token);
bool is_nift_statement_line(const std::vector<std::string>& tokens,
                            const std::string& raw_line,
                            const ShellSnapshot& snapshot);
Decision classify_segment(const std::vector<std::string>& tokens,
                          const ShellSnapshot& snapshot,
                          bool command_line = false);
LinePlan classify_line(const std::vector<std::string>& tokens,
                       const ShellSnapshot& snapshot);
// operator_token[i] is true only for an unquoted/unescaped shell operator.
LinePlan classify_line(const std::vector<std::string>& tokens,
                       const std::vector<bool>& operator_token,
                       const ShellSnapshot& snapshot);

// Whole-line syntax validation. Pure; no interpolation or dispatch. Returns
// false and sets `error` to `nift: syntax error near '<token>'` for a stray
// connector/pipe/background operator or a redirection without an operand.
// A trailing `;` is valid; quoted/escaped operator words are ordinary words.
bool validate_line_syntax(const std::vector<std::string>& tokens,
                          const std::vector<bool>& operator_token,
                          std::string& error);

const char* command_kind_name(CommandKind kind);

}  // namespace nift::shell
