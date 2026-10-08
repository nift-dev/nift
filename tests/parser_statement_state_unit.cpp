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

    // CP-F: nested execution retains the original definition document. Public
    // fields and typed origins agree; compatibility messages and frames stay frozen.
    const auto check_exact_failure = [](const RenderResult& result,
                                        const std::filesystem::path& source,
                                        const std::string& source_line,
                                        const std::string& message, std::size_t column = 1, std::size_t length = 1) {
        assert(!result.ok);
        assert(result.error.message == message);
        assert(result.error.source_file == source);
        assert(result.error.line == 1);
        assert(result.error.column == column);
        assert(result.error.source_line == source_line);
        assert(result.error.source_length == length);
        assert(result.diagnostic);
        const auto projected=nift::detail::project_diagnostic(*result.diagnostic);
        assert(projected == message);
        assert(result.diagnostic->origin.source == source);
        assert(result.diagnostic->origin.line == result.error.line);
        assert(result.diagnostic->origin.column == column);
        assert(result.diagnostic->origin.source_length == length);
        assert(result.diagnostic->origin.source_line == source_line);
    };
    ScriptRenderHost diagnostic_host(std::filesystem::current_path());
    TrackedInfo diagnostic_tracked;
    Parser diagnostic_parser(diagnostic_host, diagnostic_tracked);
    const auto has_frame=[](const RenderResult& result,nift::detail::DiagnosticFrameKind kind){assert(result.diagnostic);for(const auto& frame:result.diagnostic->frames)if(frame.kind==kind)return true;return false;};
    check_exact_failure(diagnostic_parser.run_statement("missing_value", "<direct>"),
                        "<direct>", "missing_value",
                        "unknown value or malformed expression: missing_value", 1, 13);
    assert(diagnostic_parser.run_statement(
        "fn(bad_function()) { return 1 / 0 }", "<define-function>").ok);
    const auto function_failure=diagnostic_parser.run_statement("bad_function()", "<function-call>");
    check_exact_failure(function_failure,
                        "<define-function>", "fn(bad_function()) { return 1 / 0 }", "return: division by zero", 22);
    assert(has_frame(function_failure,nift::detail::DiagnosticFrameKind::Callable));
    assert(diagnostic_parser.run_statement(
        "struct(bad_box) { fn(fail()) { return 1 / 0 } }", "<define-method>").ok);
    assert(diagnostic_parser.run_statement("box := bad_box()", "<make-method>").ok);
    const auto method_failure=diagnostic_parser.run_statement("box.fail()", "<method-call>");
    check_exact_failure(method_failure,
                        "<define-method>", "struct(bad_box) { fn(fail()) { return 1 / 0 } }", "return: division by zero", 32);
    assert(has_frame(method_failure,nift::detail::DiagnosticFrameKind::Method));
    assert(diagnostic_parser.run_statement(
        "bad_lambda := () => { return 1 / 0 }", "<define-lambda>").ok);
    const auto lambda_failure=diagnostic_parser.run_statement("bad_lambda()", "<lambda-call>");
    check_exact_failure(lambda_failure,
                        "<define-lambda>", "bad_lambda := () => { return 1 / 0 }", "return: division by zero", 23);
    assert(has_frame(lambda_failure,nift::detail::DiagnosticFrameKind::Lambda));
    assert(diagnostic_parser.run_statement(
        "fn(bad_callback(value)) { return 1 / 0 }", "<define-callback>").ok);
    const auto callback_failure=diagnostic_parser.run_statement("[1].map(bad_callback)", "<callback-call>");
    check_exact_failure(
        callback_failure,
        "<define-callback>", "fn(bad_callback(value)) { return 1 / 0 }", "return: division by zero", 27);
    assert(has_frame(callback_failure,nift::detail::DiagnosticFrameKind::Callback));
    const auto script_failure=diagnostic_parser.run_statement("@script { missing_value }", "<script-block>");
    check_exact_failure(
        script_failure,
        "<script-block>", "@script { missing_value }",
        "unknown value or malformed expression: missing_value", 11, 13);
    assert(has_frame(script_failure,nift::detail::DiagnosticFrameKind::Script));

    assert(diagnostic_parser.run_statement("worker := thread(bad_function)",
                                           "<thread-create>").ok);
    for (int observation = 0; observation < 2; ++observation) {
        const auto failure=diagnostic_parser.run_statement("worker.join()", "<thread-join>");
        check_exact_failure(failure,
                            "<define-function>", "fn(bad_function()) { return 1 / 0 }",
                            "thread: return: division by zero", 22);
        assert(has_frame(failure,nift::detail::DiagnosticFrameKind::Thread));
    }
    assert(diagnostic_parser.run_statement(
        "@fn[async](bad_future()) { return 1 / 0 }", "<define-future>").ok);
    assert(diagnostic_parser.run_statement("pending := bad_future()", "<future-create>").ok);
    for (int observation = 0; observation < 2; ++observation) {
        const auto failure=diagnostic_parser.run_statement("await pending", "<future-await>");
        check_exact_failure(failure,
                            "<define-future>", "@fn[async](bad_future()) { return 1 / 0 }",
                            "future: return: division by zero", 28);
        assert(has_frame(failure,nift::detail::DiagnosticFrameKind::Future));
    }
    assert(diagnostic_parser.run_statement(
        "fn(timer_worker()) { return timer() }", "<define-timer-worker>").ok);
    assert(diagnostic_parser.run_statement(
        "timer_thread := thread(timer_worker)", "<create-timer-worker>").ok);
    const auto synthesized_worker_failure=diagnostic_parser.run_statement(
        "timer_thread.join()", "<join-timer-worker>");
    assert(!synthesized_worker_failure.ok);
    assert(synthesized_worker_failure.diagnostic);
    assert(has_frame(synthesized_worker_failure,nift::detail::DiagnosticFrameKind::Thread));
    assert(synthesized_worker_failure.diagnostic->origin.source=="<define-timer-worker>");
    assert(synthesized_worker_failure.diagnostic->origin.line==1);

    // CP7 M1: a built-in recoverable worker failure must preserve its built-in
    // DiagnosticCode through await/join instead of being flattened to UserRaised,
    // while a user.* error keeps the UserRaised umbrella internally.
    assert(diagnostic_parser.run_statement(
        "fn[async](builtin_async()) { import(\"./missing-worker.f\") }", "<define-builtin-async>").ok);
    assert(diagnostic_parser.run_statement("bh := builtin_async()", "<create-builtin-async>").ok);
    const auto builtin_await=diagnostic_parser.run_statement("await bh", "<await-builtin>");
    assert(!builtin_await.ok&&builtin_await.diagnostic);
    assert(builtin_await.diagnostic->code==nift::detail::DiagnosticCode::IoImportSourceUnreadable);
    assert(has_frame(builtin_await,nift::detail::DiagnosticFrameKind::Future));
    assert(diagnostic_parser.run_statement(
        "fn[async](user_async()) { throw error(\"x\", \"user.custom\") }", "<define-user-async>").ok);
    assert(diagnostic_parser.run_statement("uh := user_async()", "<create-user-async>").ok);
    const auto user_await=diagnostic_parser.run_statement("await uh", "<await-user>");
    assert(!user_await.ok&&user_await.diagnostic);
    assert(user_await.diagnostic->code==nift::detail::DiagnosticCode::UserRaised);
    assert(has_frame(user_await,nift::detail::DiagnosticFrameKind::Future));
    diagnostic_parser.finalize_execution_workers();

    // A failed outer import may have completed child imports and allocated
    // escaping lambdas before its own export validation fails. Repeating that
    // failure must leave the persistent parser clean, and the next import must
    // receive a usable, non-colliding module/callable identity.
    const auto root=std::filesystem::temp_directory_path()/("nift-import-rollback-unit-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;std::filesystem::remove_all(root,ec);std::filesystem::create_directories(root,ec);assert(!ec);
    {std::ofstream child(root/"child.f");child<<"fn(private_value()) { return 42 }\ncallback := private_value\nexport(callback)\n";}
    {std::ofstream outer(root/"outer.f");outer<<"import(\"./child.f\")\nleaked := () => { return callback() }\nexport(missing)\n";}
    {std::ofstream bad(root/"bad.f");bad<<"missing_value\n";}
    {std::ofstream bad_outer(root/"bad-outer.f");bad_outer<<"import(\"./bad.f\")\n";}
    ScriptRenderHost import_host(root);TrackedInfo import_tracked;Parser import_parser(import_host,import_tracked);
    const auto missing_import=import_parser.run_statement("import(\"./missing.f\")",root/"main.f");
    assert(!missing_import.ok&&missing_import.diagnostic);
    assert(missing_import.diagnostic->code==nift::detail::DiagnosticCode::IoImportSourceUnreadable);
    assert(has_frame(missing_import,nift::detail::DiagnosticFrameKind::Import));
    assert(nift::detail::project_diagnostic(*missing_import.diagnostic)==missing_import.error.message);
    const auto file_backed_path=import_parser.run_statement("module_path()",root/"main.f");
    assert(file_backed_path.ok&&file_backed_path.output==nift::RuntimeValue(root.generic_string()).dump(0));
    const auto in_memory_path=import_parser.run_statement("module_path()","<statement-resource-test>");
    assert(!in_memory_path.ok&&in_memory_path.error.message.find("no file-backed source")!=std::string::npos);
    const auto import_failure=import_parser.run_statement("import(\"./bad.f\")",root/"main.f");
    check_exact_failure(import_failure,
                        root/"bad.f", "missing_value",
                        "import: unknown value or malformed expression: missing_value", 1, 13);
    assert(has_frame(import_failure,nift::detail::DiagnosticFrameKind::Import));
    check_exact_failure(import_parser.run_statement("import(\"./bad-outer.f\")",root/"main.f"),
                        root/"bad.f", "missing_value",
                        "import: import: unknown value or malformed expression: missing_value", 1, 13);
    for(int attempt=0;attempt<2;++attempt){const auto failed=import_parser.run_statement("import(\"./outer.f\")",root/"main.f");assert(!failed.ok);assert(failed.error.message.find("export names no existing binding: missing")!=std::string::npos);}
    assert(import_parser.run_statement("import(\"./child.f\")",root/"main.f").ok);
    assert(import_parser.run_statement("answer := callback()",root/"main.f").ok);
    nift::RuntimeValue answer;std::string eval_error;assert(import_parser.eval_expression("answer",answer,eval_error));assert(answer.is_number()&&answer.num==42);

    // package.not_installed: the import frame label is the attempted package
    // root (never empty), and the recoverable diagnostic is catchable.
    {
        const auto old_cwd=std::filesystem::current_path();
        std::error_code pec;
        const std::filesystem::path proot=old_cwd/".nift-b4-cp5b-pkg";
        std::filesystem::remove_all(proot,pec);
        std::filesystem::create_directories(proot/".nift"/"packages"/"demo",pec);
        {std::ofstream m(proot/"manifest.json");m<<"{\"dependencies\":{\"demo\":{\"source\":\"./demo\",\"ref\":\"local\"}}}\n";}
        {std::ofstream l(proot/".nift"/"packages.lock.json");l<<"{\"demo\":{\"source\":\"./demo\",\"requested\":\"local\",\"commit\":\"local\"}}\n";}
        std::filesystem::current_path(proot);
        ScriptRenderHost package_host(proot);TrackedInfo package_tracked;Parser package_parser(package_host,package_tracked);
        const auto not_installed=package_parser.run_statement("import(\"demo\")",proot/"main.f");
        std::filesystem::current_path(old_cwd);
        std::filesystem::remove_all(proot,pec);
        assert(!not_installed.ok&&not_installed.diagnostic);
        assert(not_installed.diagnostic->code==nift::detail::DiagnosticCode::PackageNotInstalled);
        assert(nift::detail::diagnostic_code_info(not_installed.diagnostic->code).disposition==nift::detail::DiagnosticDisposition::Recoverable);
        bool frame_label_ok=false;
        for(const auto& f:not_installed.diagnostic->frames)
            if(f.kind==nift::detail::DiagnosticFrameKind::Import&&!f.label.empty()&&f.label.find("demo")!=std::string::npos)frame_label_ok=true;
        assert(frame_label_ok);
    }

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
