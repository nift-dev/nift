#include <nift/nift.h>

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

int main() {
    std::ostringstream leaked_stdout;
    std::ostringstream leaked_stderr;
    auto* original_stdout = std::cout.rdbuf(leaked_stdout.rdbuf());
    auto* original_stderr = std::cerr.rdbuf(leaked_stderr.rdbuf());
    nift::Engine engine;
    auto executed = engine.execute(
        "print(\"execute-out\"); err(\"execute-err\"); return 42");
    assert(executed.ok());
    assert(executed.value().number() == 42);
    assert(executed.stdout_output() == "execute-out\n");
    assert(executed.stderr_output() == "execute-err\n");

    auto evaluated = engine.evaluate("print(\"evaluate-out\")");
    assert(evaluated.ok());
    assert(evaluated.stdout_output() == "evaluate-out\n");
    assert(evaluated.stderr_output().empty());

    auto failed = engine.execute(
        "print(\"before-out\"); err(\"before-err\"); return 1 / 0");
    assert(!failed.ok());
    assert(failed.stdout_output() == "before-out\n");
    assert(failed.stderr_output() == "before-err\n");

    auto workers = engine.execute(
        "fn(late()) { sleep(20); print(\"late-out\"); err(\"late-err\"); return 1 }\n"
        "t := thread(late)\nreturn 0");
    assert(workers.ok());
    assert(workers.stdout_output() == "late-out\n");
    assert(workers.stderr_output() == "late-err\n");

    auto definitions = engine.execute(
        "fn(log_value(x)) { print(x); err(x); return x }\nreturn null");
    assert(definitions.ok());
    auto called = engine.call("log_value", {nift::Value("called")});
    assert(called.ok());
    assert(called.stdout_output() == "called\n");
    assert(called.stderr_output() == "called\n");

    auto rendered = engine.render_text(
        "@script{ print(\"render-out\"); err(\"render-err\") }visible");
    assert(rendered.ok());
    assert(rendered.output() == "visible");
    assert(rendered.stdout_output() == "render-out\n");
    assert(rendered.stderr_output() == "render-err\n");

    std::vector<std::thread> threads;
    std::vector<nift::RenderResult> results(8);
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i] {
            const std::string token = std::to_string(i);
            results[i] = engine.render_text("@script{ print(\"out-" + token +
                                            "\"); err(\"err-" + token + "\") }");
        });
    }
    for (auto& thread : threads) thread.join();
    for (std::size_t i = 0; i < results.size(); ++i) {
        const std::string token = std::to_string(i);
        assert(results[i].ok());
        assert(results[i].stdout_output() == "out-" + token + "\n");
        assert(results[i].stderr_output() == "err-" + token + "\n");
    }
    std::cout.rdbuf(original_stdout);
    std::cerr.rdbuf(original_stderr);
    assert(leaked_stdout.str().empty());
    assert(leaked_stderr.str().empty());
    return 0;
}
