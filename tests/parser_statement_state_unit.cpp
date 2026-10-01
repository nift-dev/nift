#include "Parser.h"
#include "PackageTransaction.h"
#include "ScriptHost.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
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
    const auto file_backed_path=import_parser.run_statement("module_path()",root/"main.f");
    assert(file_backed_path.ok&&file_backed_path.output==nift::RuntimeValue(root.generic_string()).dump(0));
    const auto in_memory_path=import_parser.run_statement("module_path()","<statement-resource-test>");
    assert(!in_memory_path.ok&&in_memory_path.error.message.find("no file-backed source")!=std::string::npos);
    for(int attempt=0;attempt<2;++attempt){const auto failed=import_parser.run_statement("import(\"./outer.f\")",root/"main.f");assert(!failed.ok);assert(failed.error.message.find("export names no existing binding: missing")!=std::string::npos);}
    assert(import_parser.run_statement("import(\"./child.f\")",root/"main.f").ok);
    assert(import_parser.run_statement("answer := callback()",root/"main.f").ok);
    nift::RuntimeValue answer;std::string eval_error;assert(import_parser.eval_expression("answer",answer,eval_error));assert(answer.is_number()&&answer.num==42);

    // Failed nested execution owns only the FileValues allocated inside it.
    // A caller's dirty open file remains recoverable, while function/@script
    // locals are removed rather than surviving until parser teardown.
    {std::ofstream caller(root/"caller.txt");caller<<"BASE";}
    const std::string caller_path=(root/"caller.txt").generic_string();
    const std::string function_path=(root/"function-local.txt").generic_string();
    const std::string rejected_path=(root/"rejected-local.txt").generic_string();
    const std::string script_path=(root/"script-local.txt").generic_string();
    assert(import_parser.run_statement("caller_file := file(\""+caller_path+"\")",root/"main.f").ok);
    assert(import_parser.run_statement("caller_file.open(\"rw\")",root/"main.f").ok);
    assert(import_parser.run_statement("caller_file.replace_once(\"BASE\", \"DIRTY\")",root/"main.f").ok);
    assert(import_parser.run_statement("fn(leak_file()) { local := file(\""+function_path+"\"); local.open(\"w\"); missing_value }",root/"main.f").ok);
    assert(!import_parser.run_statement("leak_file()",root/"main.f").ok);
    nift::RuntimeValue modified;assert(import_parser.eval_expression("caller_file.modified()",modified,eval_error)&&modified.is_bool()&&modified.boolean);
    assert(import_parser.run_statement("caller_file.revert()",root/"main.f").ok);
    assert(import_parser.run_statement("caller_file.close()",root/"main.f").ok);
    std::string resource_error;assert(import_parser.finalize_script_resources(resource_error));
    assert(import_parser.run_statement("fn(rejected_file()) { local := file(\""+rejected_path+"\"); local.open(\"w\"); return timer() }",root/"main.f").ok);
    assert(!import_parser.run_statement("rejected_file()",root/"main.f").ok);
    resource_error.clear();assert(import_parser.finalize_script_resources(resource_error));
    const auto failed_script=import_parser.run_statement("@script { local := file(\""+script_path+"\"); local.open(\"w\"); missing_value }",root/"main.f");
    assert(!failed_script.ok);
    resource_error.clear();assert(import_parser.finalize_script_resources(resource_error));

    // A failed package import must roll back its ModuleEnv and release the read
    // lease even while the persistent parser remains alive.
    const auto package=root/".nift/packages/demo";std::filesystem::create_directories(package/"src",ec);assert(!ec);
    {std::ofstream project_manifest(root/"manifest.json");project_manifest<<"{\"dependencies\":{\"demo\":{\"source\":\"./demo\",\"ref\":\"local\"}}}\n";}
    {std::ofstream package_lock(root/".nift/packages.lock.json");package_lock<<"{\"demo\":{\"source\":\"./demo\",\"requested\":\"local\",\"commit\":\"local\"}}\n";}
    {std::ofstream manifest(package/"manifest.json");manifest<<"{\"name\":\"demo\",\"version\":\"0.1.0\",\"entry\":\"src/main.f\"}\n";}
    {std::ofstream module(package/"src/main.f");module<<"value := 1\nexport(missing)\n";}
    auto package_parser=std::make_unique<Parser>(import_host,import_tracked);
    const auto saved_cwd=std::filesystem::current_path();std::filesystem::current_path(root);
    const auto package_failed=package_parser->run_statement("import(\"demo\")",root/"main.f");assert(!package_failed.ok);
    std::filesystem::current_path(saved_cwd);
    auto exclusive=std::async(std::launch::async,[&root]{PackageTransaction transaction(root);std::string error;return transaction.acquire(error);});
    const bool lease_released=exclusive.wait_for(std::chrono::seconds(2))==std::future_status::ready;
    package_parser.reset();
    assert(exclusive.wait_for(std::chrono::seconds(2))==std::future_status::ready);
    assert(lease_released&&exclusive.get());
    std::filesystem::remove_all(root,ec);
    return 0;
}
