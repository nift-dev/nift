// CP8: embedding / C ABI boundary certification for the v4.6 recoverable
// error model. Proves that every new recoverable/fatal outcome projects
// through the public nift::Engine as an ordinary failed ScriptResult with the
// correct message/origin and NO interpreter-internal Error transport, and that
// recursive Error values are rejected (never stringified/coerced) at the
// public boundary. No ABI surface is changed.
#include <nift/nift.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "v46-b4-cp8-embed FAIL: %s (line %d)\n",    \
                         #cond, __LINE__);                                   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

int main() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string tag = std::to_string(static_cast<long>(::getpid()));

    // 1. Uncaught built-in recoverables project as normal failed results with
    //    a human-readable message/origin; no Error value can escape.
    {
        nift::Engine engine;
        auto r = engine.execute("open(\"nift-cp8-missing-" + tag + ".txt\")");
        CHECK(!r.ok());
        CHECK(!r.error().message.empty());
        CHECK(!r.error().source.empty());
    }
    {
        nift::Engine engine;
        auto r = engine.execute("s := ifstream(\"nift-cp8-missing-" + tag + ".txt\")");
        CHECK(!r.ok());
        CHECK(contains(r.error().message, "open") || contains(r.error().message, "read"));
    }
    {
        nift::Engine engine;
        auto r = engine.execute("validate({\"type\":\"string\"}, 42)");
        CHECK(!r.ok());
        CHECK(contains(r.error().message, "expected string") || contains(r.error().message, "received number"));
    }
    {
        nift::Engine engine;
        auto r = engine.execute("lib := ffi_open(\"nift-cp8-no-such-lib-" + tag + ".so\")");
        CHECK(!r.ok());
        CHECK(!r.error().message.empty());
    }
    {
        nift::Engine engine;
        auto r = engine.execute("throw error(\"user error\", \"user.cp8\")");
        CHECK(!r.ok());
        CHECK(contains(r.error().message, "user error"));
    }

    // 2. Import-source and package recoverables through a file-backed project.
    {
        fs::path root = fs::temp_directory_path() / ("nift-cp8-import-" + tag);
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
        { std::ofstream f(root / "main.f"); f << "import(\"./missing-" << tag << ".f\")\n"; }
        nift::Engine engine(root);
        auto r = engine.execute("import(\"./missing-" + tag + ".f\")", (root / "main.f").string());
        CHECK(!r.ok());
        CHECK(contains(r.error().message, "not readable") || contains(r.error().message, "unreadable"));
        fs::remove_all(root, ec);
    }
    {
        fs::path root = fs::temp_directory_path() / ("nift-cp8-pkg-" + tag);
        fs::remove_all(root, ec);
        fs::create_directories(root / ".nift" / "packages" / "demo", ec);
        { std::ofstream f(root / "manifest.json"); f << "{\"dependencies\":{\"demo\":{\"source\":\"./demo\",\"ref\":\"local\"}}}\n"; }
        { std::ofstream f(root / ".nift" / "packages.lock.json"); f << "{\"demo\":{\"source\":\"./demo\",\"requested\":\"local\",\"commit\":\"local\"}}\n"; }
        { std::ofstream f(root / "main.f"); f << "import(\"demo\")\n"; }
        nift::Engine engine(root);
        // Embedded hosted mode performs no shell package resolution, so a
        // package-name import projects the unresolvable import source as an
        // ordinary failed result. package.not_installed itself is certified
        // on the CLI side (CP5b wall).
        auto r = engine.execute("import(\"demo\")", (root / "main.f").string());
        CHECK(!r.ok());
        CHECK(!r.error().message.empty());
        CHECK(contains(r.error().message, "not readable") || contains(r.error().message, "not installed"));
        fs::remove_all(root, ec);
    }

    // 3. Worker outcomes through embedding (CP7 completion diagnostics).
    {
        nift::Engine engine;
        // built-in recoverable produced in an async worker, uncaught at top
        auto r = engine.execute(
            "fn[async](af()) { open(\"nift-cp8-worker-missing-" + tag + ".txt\") }\n"
            "h := af()\n"
            "await h\n");
        CHECK(!r.ok());
        CHECK(!r.error().message.empty());
    }
    {
        nift::Engine engine;
        // worker built-in recoverable caught, script then succeeds normally
        auto r = engine.execute(
            "fn[async](af()) { open(\"nift-cp8-worker-missing-" + tag + ".txt\") }\n"
            "h := af()\n"
            "try { await h } catch(e) {}\n"
            "return 42\n");
        CHECK(r.ok());
        CHECK(r.value().is_number());
        CHECK(r.value().number() == 42);
    }
    {
        nift::Engine engine;
        // worker fatal failure bypasses catch at the embedding boundary
        auto r = engine.execute(
            "fn[async](af()) { return 1 / 0 }\n"
            "h := af()\n"
            "try { await h } catch(e) {}\n"
            "return 7\n");
        CHECK(!r.ok());
        CHECK(contains(r.error().message, "division by zero"));
    }

    // 4. Context reuse after recoverable and fatal failures: a subsequent
    //    successful operation returns a normal success result.
    {
        nift::Engine engine;
        CHECK(!engine.execute("open(\"nift-cp8-reuse-missing-" + tag + ".txt\")").ok());
        auto ok = engine.execute("return 7");
        CHECK(ok.ok());
        CHECK(ok.value().is_number() && ok.value().number() == 7);
        CHECK(!engine.execute("value := 1 / 0").ok());
        auto ok2 = engine.execute("return 8");
        CHECK(ok2.ok());
        CHECK(ok2.value().is_number() && ok2.value().number() == 8);
    }

    // 5. Recursive Error rejection is already covered by the CP3 embed wall
    //    (direct/array/object/collection/struct). Re-assert the essentials:
    //    a nested Error in a returned graph is rejected, never stringified.
    {
        nift::Engine engine;
        auto loaded = engine.execute(
            "fn(nested_error()) { return {\"ok\": true, \"inner\": [error(\"nested\")]} }\n"
            "return 0\n");
        CHECK(loaded.ok());
        auto r = engine.call("nested_error", {});
        CHECK(!r.ok());
        CHECK(contains(r.error().message, "Error value"));
    }

    if (failures) {
        std::fprintf(stderr, "v46-b4-cp8-embed: %d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "v46-b4-cp8-embed: PASS\n");
    return 0;
}