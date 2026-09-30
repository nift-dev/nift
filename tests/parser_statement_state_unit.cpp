#include "Parser.h"
#include "ScriptHost.h"

#include <cassert>
#include <filesystem>
#include <string>

int main() {
    ScriptRenderHost host(std::filesystem::current_path());
    TrackedInfo tracked;
    Parser parser(host, tracked);
    using State = Parser::StatementState;

    assert(parser.statement_state("") == State::Complete);
    assert(parser.statement_state("value := 1") == State::Complete);
    assert(parser.statement_state("if (true) { return 1 }") == State::Complete);
    assert(parser.statement_state("print(\"}\")") == State::Complete);

    assert(parser.statement_state("print(") == State::Incomplete);
    assert(parser.statement_state("items[0") == State::Incomplete);
    assert(parser.statement_state("if (true) {") == State::Incomplete);
    assert(parser.statement_state("\"unterminated") == State::Incomplete);

    assert(parser.statement_state(")") == State::Invalid);
    assert(parser.statement_state("if true { return 1 }") == State::Invalid);
    assert(parser.statement_state("if (true) return 1") == State::Invalid);
    assert(parser.statement_state("while true {}") == State::Invalid);
    assert(parser.statement_state("fn(example())") == State::Invalid);

    const auto check_diagnostic = [&](const std::string& source, const std::string& expected) {
        const RenderResult result = parser.run_statement(source, "<statement-state-test>");
        assert(!result.ok);
        assert(result.error.message == expected);
    };
    check_diagnostic("if true { return 1 }", "function if requires '(...)'");
    check_diagnostic("if (true) return 1", "function if requires a block");
    check_diagnostic("while true {}", "function while requires '(...)'");
    check_diagnostic("fn(example())", "fn requires a block");
    return 0;
}
