#include "Parser.h"
#include "ScriptHost.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
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

    // A failed outer import may have completed child imports and allocated
    // escaping lambdas before its own export validation fails. Repeating that
    // failure must leave the persistent parser clean, and the next import must
    // receive a usable, non-colliding module/callable identity.
    const auto root=std::filesystem::temp_directory_path()/("nift-import-rollback-unit-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;std::filesystem::remove_all(root,ec);std::filesystem::create_directories(root,ec);assert(!ec);
    {std::ofstream child(root/"child.f");child<<"fn(private_value()) { return 42 }\ncallback := private_value\nexport(callback)\n";}
    {std::ofstream outer(root/"outer.f");outer<<"import(\"./child.f\")\nleaked := () => { return callback() }\nexport(missing)\n";}
    ScriptRenderHost import_host(root);TrackedInfo import_tracked;Parser import_parser(import_host,import_tracked);
    for(int attempt=0;attempt<2;++attempt){const auto failed=import_parser.run_statement("import(\"./outer.f\")",root/"main.f");assert(!failed.ok);assert(failed.error.message.find("export names no existing binding: missing")!=std::string::npos);}
    assert(import_parser.run_statement("import(\"./child.f\")",root/"main.f").ok);
    assert(import_parser.run_statement("answer := callback()",root/"main.f").ok);
    nift::RuntimeValue answer;std::string eval_error;assert(import_parser.eval_expression("answer",answer,eval_error));assert(answer.is_number()&&answer.num==42);
    std::filesystem::remove_all(root,ec);
    return 0;
}
