#include <nift/nift.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "nift-v46-resource-paths-embed";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root / "modules", error);
    assert(!error);
    {
        std::ofstream module(root / "modules" / "owner.f");
        module << "fn(resource()) { return module_path(\"asset.txt\") }\n"
                  "fn(thread_resource()) { return module_path(\"asset-thread.txt\") }\n"
                  "fn(thread_escape()) { return module_path(\"../../escape.txt\") }\n"
                  "future_resource := async () => module_path(\"asset-future.txt\")\n"
                  "future_escape := async () => module_path(\"../../escape.txt\")\n"
                  "export(resource)\nexport(thread_resource)\nexport(thread_escape)\n"
                  "export(future_resource)\nexport(future_escape)\n";
    }
    {
        std::ofstream partial(root / "partial.html");
        partial << "$[module_path()]";
    }
    {
        std::ofstream collision_template(root / "collision.html");
        collision_template << "@input(\"partial.html\")|@content";
    }

    nift::Engine source_less(root);
    auto execute_missing = source_less.execute("return module_path()");
    assert(!execute_missing.ok());
    assert(execute_missing.error().message.find("no file-backed source") != std::string::npos);
    auto evaluate_missing = source_less.evaluate("module_path()");
    assert(!evaluate_missing.ok());
    assert(evaluate_missing.error().message.find("no file-backed source") != std::string::npos);
    auto text_missing = source_less.render_text("$[module_path()]");
    assert(!text_missing.ok());
    assert(text_missing.error().message.find("no file-backed source") != std::string::npos);
    auto named_text_missing = source_less.render(nift::Source::text("$[module_path()]", "logical/partial.html"));
    assert(!named_text_missing.ok());
    assert(named_text_missing.error().message.find("no file-backed source") != std::string::npos);
    nift::Engine thread_source_less(root);
    auto thread_text_missing = thread_source_less.execute(
        "fn(no_source()) { return module_path() }\n"
        "missing_worker := thread(no_source)\nreturn missing_worker.join()");
    assert(!thread_text_missing.ok());
    assert(thread_text_missing.error().message.find("no file-backed source") != std::string::npos);
    nift::Engine future_source_less(root);
    auto future_text_missing = future_source_less.execute(
        "no_source := async () => module_path()\n"
        "missing_future := no_source()\nreturn await missing_future");
    assert(!future_text_missing.ok());
    assert(future_text_missing.error().message.find("no file-backed source") != std::string::npos);

    nift::Engine engine(root);
    auto imported = engine.execute("import(\"./modules/owner.f\")\nreturn resource()");
    assert(imported.ok());
    assert(imported.value().is_string());
    assert(imported.value().string() == (root / "modules" / "asset.txt").generic_string());
    auto threaded = engine.execute(
        "import(\"./modules/owner.f\")\nresource_worker := thread(thread_resource)\nreturn resource_worker.join()");
    assert(threaded.ok());
    assert(threaded.value().string() == (root / "modules" / "asset-thread.txt").generic_string());
    auto thread_escape = engine.execute(
        "import(\"./modules/owner.f\")\nescape_worker := thread(thread_escape)\nreturn escape_worker.join()");
    // D-051: no default project-root containment. Without an explicit
    // filesystem root an outside module path resolves like any other path.
    const std::string outside = (root / "modules" / ".." / ".." / "escape.txt").lexically_normal().generic_string();
    assert(thread_escape.ok());
    assert(thread_escape.value().string() == outside);
    auto futured = engine.execute(
        "import(\"./modules/owner.f\")\nresource_future := future_resource()\nreturn await resource_future");
    assert(futured.ok());
    assert(futured.value().string() == (root / "modules" / "asset-future.txt").generic_string());
    auto future_escape_result = engine.execute(
        "import(\"./modules/owner.f\")\nescape_future := future_escape()\nreturn await escape_future");
    assert(future_escape_result.ok());
    assert(future_escape_result.value().string() == outside);

    auto rendered = engine.render(nift::Source::path("partial.html"));
    assert(rendered.ok());
    assert(rendered.output() == root.generic_string());
    auto collision = engine.render(
        nift::Source::text("page", (root / "partial.html").generic_string()),
        nift::Source::path("collision.html"));
    assert(collision.ok());
    assert(collision.output() == root.generic_string() + "|page");

    {
        std::ofstream escape(root / "modules" / "escape.f");
        escape << "fn(resource()) { return module_path(\"../../escape.txt\") }\nexport(resource)\n";
    }
    nift::Engine unconfined(root);
    auto escaped = unconfined.execute("import(\"./modules/escape.f\")\nreturn resource()");
    assert(escaped.ok());
    assert(escaped.value().string() == outside);

    // The explicit opt-in restriction (NIFT_FS_ROOT, as --fs-root sets it) is
    // installed here and must reject every escape, while in-root paths work.
#ifdef _WIN32
    _putenv_s("NIFT_FS_ROOT", root.string().c_str());
#else
    setenv("NIFT_FS_ROOT", root.string().c_str(), 1);
#endif
    {
        const auto restricted_error = [](const auto& result) {
            return !result.ok() &&
                result.error().message.find("escapes configured filesystem root") != std::string::npos;
        };
        nift::Engine confined(root);
        auto inside = confined.execute("import(\"./modules/owner.f\")\nreturn resource()");
        assert(inside.ok());
        assert(inside.value().string() == (root / "modules" / "asset.txt").generic_string());
        assert(restricted_error(confined.execute("import(\"./modules/escape.f\")\nreturn resource()")));
        assert(restricted_error(confined.execute(
            "import(\"./modules/owner.f\")\nescape_worker := thread(thread_escape)\nreturn escape_worker.join()")));
        assert(restricted_error(confined.execute(
            "import(\"./modules/owner.f\")\nescape_future := future_escape()\nreturn await escape_future")));
    }
#ifdef _WIN32
    _putenv_s("NIFT_FS_ROOT", "");
#else
    unsetenv("NIFT_FS_ROOT");
#endif

    fs::remove_all(root, error);
    return 0;
}
