// CP19 Slice 1: pure command-dispatch classifier tests (D-042).
#include "shell/CommandDispatch.h"

#include <iostream>
#include <set>
#include <string>
#include <vector>

using nift::shell::CommandKind;
using nift::shell::Decision;
using nift::shell::LinePlan;
using nift::shell::ShellSnapshot;

namespace {

int checks = 0;
int failures = 0;

void expect(bool condition, const std::string& what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << what << '\n';
    }
}

ShellSnapshot snapshot() {
    ShellSnapshot s;
    s.shell_builtins = {"cd", "exit", "quit", "jobs", "fg", "bg", "wait"};
    s.command_builtins = {"print", "pwd"};
    s.nift_expression_names = {"true", "false", "null", "build", "timer",
                               "module_path", "package_path"};
    s.user_functions = {"myfn", "fixtool"};
    const std::set<std::string> executables = {
        "ls", "mkdir", "cat", "cp", "mv", "rm", "echo", "true", "false",
        "printf", "wc", "fixtool", "./x", "/bin/x", "dir/x",
    };
    s.executable_exists = [executables](const std::string& name) {
        return executables.count(name) != 0;
    };
    return s;
}

void expect_kind(const std::vector<std::string>& tokens, CommandKind kind,
                 const std::string& what, bool command_line = true) {
    const Decision decision = nift::shell::classify_segment(tokens, snapshot(), command_line);
    expect(decision.kind == kind, what + " (got " +
           nift::shell::command_kind_name(decision.kind) + ")");
}

void test_segments() {
    expect_kind({"print", "hi"}, CommandKind::NiftBuiltin, "print command form");
    expect_kind({"pwd"}, CommandKind::NiftBuiltin, "pwd command form");
    expect_kind({"cd", "/tmp"}, CommandKind::ShellBuiltin, "cd shell state");
    expect_kind({"jobs"}, CommandKind::ShellBuiltin, "jobs shell state");
    expect_kind({"myfn", "x"}, CommandKind::UserFunction, "user function");
    expect_kind({"ls", "-l"}, CommandKind::External, "ls PATH");
    expect_kind({"mkdir", "x"}, CommandKind::External, "mkdir PATH");
    expect_kind({"cat", "f"}, CommandKind::External, "cat PATH");
    expect_kind({"command", "mkdir", "x"}, CommandKind::External, "command bypass");
    expect_kind({"command", "print", "hi"}, CommandKind::NotFound, "command print external-only");
    expect_kind({"./x"}, CommandKind::External, "explicit ./ path");
    expect_kind({"/bin/x"}, CommandKind::External, "explicit / path");
    expect_kind({"dir/x"}, CommandKind::External, "explicit relative path");
    expect_kind({"nope"}, CommandKind::NotFound, "unknown name");
    expect_kind({"VAR=x", "ls"}, CommandKind::External, "env assignment skipped");
    expect_kind({"true"}, CommandKind::NiftStatement, "bare true is a Nift literal", false);
    expect_kind({"true"}, CommandKind::External, "true in a command chain is external");
    expect_kind({"pwd"}, CommandKind::NiftBuiltin, "bare pwd is the command built-in", false);
    expect_kind({"myfn"}, CommandKind::UserFunction, "bare user name in a command line");

    const Decision command = nift::shell::classify_segment({"command", "ls", "-l"}, snapshot(), true);
    expect(command.command_prefix, "command prefix recorded");
    expect(command.name == "ls", "command prefix name");
    expect(command.args.size() == 1 && command.args[0] == "-l", "command prefix args");

    const Decision env = nift::shell::classify_segment({"VAR=x", "echo", "hi"}, snapshot(), true);
    expect(env.kind == CommandKind::External && env.name == "echo", "env command class");
    expect(env.args.size() == 1 && env.args[0] == "hi", "env command args");
}

void test_line_detection() {
    const ShellSnapshot s = snapshot();
    expect(nift::shell::is_nift_statement_line({"print", "hi"}, "print hi", s) == false,
           "print hi is command form");
    expect(nift::shell::is_nift_statement_line({"print(\"hello\")"}, "print(\"hello\")", s) == true,
           "print(...) is Nift");
    expect(nift::shell::is_nift_statement_line({"x", ":=", "5"}, "x := 5", s) == true,
           ":= is Nift");
    expect(nift::shell::is_nift_statement_line({"x", "=", "5"}, "x = 5", s) == true,
           "= is Nift");
    expect(nift::shell::is_nift_statement_line({"true"}, "true", s) == true,
           "bare true is Nift");
    expect(nift::shell::is_nift_statement_line({"false"}, "false", s) == true,
           "bare false is Nift");
    expect(nift::shell::is_nift_statement_line({"ls"}, "ls", s) == false,
           "bare PATH name is command form");
    expect(nift::shell::is_nift_statement_line({"myfn"}, "myfn", s) == true,
           "bare user function is Nift expression");
    expect(nift::shell::is_nift_statement_line({"fixtool"}, "fixtool", s) == true,
           "user function shadows a PATH name");
    expect(nift::shell::is_nift_statement_line({"pwd"}, "pwd", s) == false,
           "pwd is command form");
    expect(nift::shell::is_nift_statement_line({"cd", "/tmp"}, "cd /tmp", s) == false,
           "cd is command form");
    expect(nift::shell::is_nift_statement_line({"command", "ls"}, "command ls", s) == false,
           "command is command form");
    expect(nift::shell::is_nift_statement_line({"hello"}, "\"hello\"", s) == true,
           "quoted string is Nift");
    expect(nift::shell::is_nift_statement_line({"fn", "f()", "{"}, "fn f() {", s) == true,
           "fn is Nift");
    expect(nift::shell::is_nift_statement_line({"if", "(x)", "{"}, "if (x) {", s) == true,
           "if is Nift");
    expect(nift::shell::is_nift_statement_line({"echo", "$[x]"}, "echo $[x]", s) == false,
           "interpolated command is command form");
}

void test_lines() {
    const ShellSnapshot s = snapshot();
    LinePlan chain = nift::shell::classify_line({"false", "&&", "echo", "x"}, s);
    expect(chain.segments.size() == 2, "chain has two segments");
    expect(chain.segments.size() == 2 && chain.segments[0].first.kind == CommandKind::External,
           "chain first class");
    expect(chain.segments.size() == 2 && chain.segments[1].connector == "&&",
           "chain connector");
    expect(chain.segments.size() == 2 && chain.segments[1].first.kind == CommandKind::External,
           "chain second class");

    LinePlan pipeline = nift::shell::classify_line({"VAR=x", "ls", "|", "cat"}, s);
    expect(pipeline.segments.size() == 1, "pipeline one segment");
    expect(pipeline.segments.size() == 1 && pipeline.segments[0].has_pipe,
           "pipeline flag");
    expect(pipeline.segments.size() == 1 && pipeline.segments[0].first.kind == CommandKind::External,
           "pipeline first class");

    LinePlan background = nift::shell::classify_line({"echo", "a", "&"}, s);
    expect(background.segments.size() == 1 && background.segments[0].background,
           "background flag");

    LinePlan sequence = nift::shell::classify_line({"echo", "a", ";", "echo", "b"}, s);
    expect(sequence.segments.size() == 2, "semicolon segments");
    expect(sequence.segments.size() == 2 && sequence.segments[1].connector == ";",
           "semicolon connector");

    LinePlan redirection = nift::shell::classify_line({"echo", "a", ">", "out"}, s);
    expect(redirection.segments.size() == 1 && redirection.segments[0].has_redirection,
           "redirection flag");
}

void test_paths() {
    expect(nift::shell::looks_like_path("./x"), "./ path");
    expect(nift::shell::looks_like_path("../x"), "../ path");
    expect(nift::shell::looks_like_path("/bin/x"), "/ path");
    expect(nift::shell::looks_like_path("dir/x"), "relative path");
    expect(nift::shell::looks_like_path("C:\\x"), "Windows drive path");
    expect(nift::shell::looks_like_path(".\\x"), "Windows .\\ path");
    expect(!nift::shell::looks_like_path("ls"), "plain name is not a path");
    expect(nift::shell::is_nift_literal("42"), "integer literal");
    expect(nift::shell::is_nift_literal("1.5e3"), "float literal");
    expect(nift::shell::is_nift_literal("true"), "true literal");
    expect(!nift::shell::is_nift_literal("ls"), "name is not a literal");
}

}  // namespace

int main() {
    test_segments();
    test_line_detection();
    test_lines();
    test_paths();
    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "PASS shell dispatch unit: " << checks << " checks\n";
    return 0;
}
