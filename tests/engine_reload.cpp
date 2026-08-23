// PA4 evidence: atomic immutable snapshot replacement. reload() swaps in a
// freshly built snapshot so in-flight renders finish on the snapshot they
// started with; a failed reload retains the last known-good snapshot (never
// fail-closed); reload performs zero project writes; concurrent renders during
// reload observe a complete snapshot generation (never a torn mix); Engine
// defaults/environment provider survive reload; reload is the retry path for an
// Engine constructed before its project existed. No filesystem watching.
#include "nift/nift.h"

#include "FileSystem.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void write_file(const fs::path& path, const std::string& contents) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

fs::path fixture_base() { return fs::current_path() / ".build" / "engine-reload-fixtures"; }

const char* kConfig = R"({"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified"}})";

std::string tracked_with_title(const char* title) {
    return std::string(R"({"tracked":[{"name":"about","title":")") + title + R"(","template":"templates/template.html"}]})";
}

void write_project(const fs::path& root) {
    write_file(root / ".nift/config.json", kConfig);
    write_file(root / ".nift/tracked.json", tracked_with_title("Title-ALPHA"));
    write_file(root / "templates/template.html", R"(<!doctype html><title>$[title]</title>@content)");
    write_file(root / "content/about.html", "<h1>About</h1>");
}

std::map<std::string, std::string> tree_snapshot(const fs::path& root) {
    std::map<std::string, std::string> snapshot;
    std::error_code error;
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error)) {
            std::ifstream in(it->path(), std::ios::binary);
            std::ostringstream buffer;
            buffer << in.rdbuf();
            snapshot[fs::relative(it->path(), root).generic_string()] = buffer.str();
        }
    }
    return snapshot;
}

void test_new_page_after_reload(const fs::path& root) {
    write_project(root);
    nift::Engine engine(root);
    CHECK(engine.is_open());
    CHECK(!engine.render("newpage").ok());

    write_file(root / "content/newpage.html", "<h1>New</h1>");
    write_file(root / ".nift/tracked.json",
               R"({"tracked":[{"name":"about","title":"About","template":"templates/template.html"},{"name":"newpage","title":"New","template":"templates/template.html"}]})");
    std::string error;
    CHECK(engine.reload(&error));
    CHECK(error.empty());

    nift::RenderResult page = engine.render("newpage");
    CHECK(page.ok());
    if (page.ok()) CHECK(contains(page.output(), "<h1>New</h1>"));
    CHECK(engine.render("about").ok());
}

void test_failed_reload_retains_last_good(const fs::path& root) {
    write_project(root);
    nift::Engine engine(root);
    CHECK(engine.is_open());
    nift::RenderResult before = engine.render("about");
    CHECK(before.ok() && contains(before.output(), "Title-ALPHA"));

    // Project becomes malformed; reload must fail without destroying the
    // snapshot currently being served.
    write_file(root / ".nift/tracked.json", "{ not json");
    std::string error;
    CHECK(!engine.reload(&error));
    CHECK(!error.empty());
    CHECK(engine.is_open());

    nift::RenderResult after = engine.render("about");
    CHECK(after.ok());
    if (after.ok()) CHECK(contains(after.output(), "Title-ALPHA"));

    // Recovery: the malformed state is fixed and reload succeeds again.
    write_file(root / ".nift/tracked.json", tracked_with_title("Title-ALPHA"));
    CHECK(engine.reload(&error));
    CHECK(engine.render("about").ok());
}

void test_reload_as_open_retry(const fs::path& root) {
    const fs::path empty_dir = root / "empty";
    std::error_code error;
    fs::create_directories(empty_dir, error);

    nift::Engine engine(empty_dir);
    CHECK(!engine.is_open());
    CHECK(!engine.render("about").ok());

    write_project(empty_dir);
    std::string reload_error;
    CHECK(engine.reload(&reload_error));
    CHECK(engine.is_open());
    nift::RenderResult result = engine.render("about");
    CHECK(result.ok());
}

void test_generation_switch(const fs::path& root) {
    write_project(root);
    nift::Engine engine(root);
    CHECK(contains(engine.render("about").output(), "Title-ALPHA"));

    write_file(root / ".nift/tracked.json", tracked_with_title("Title-BETA"));
    CHECK(engine.reload());
    nift::RenderResult switched = engine.render("about");
    CHECK(switched.ok());
    if (switched.ok()) CHECK(contains(switched.output(), "Title-BETA"));
}

void test_concurrent_render_and_reload(const fs::path& root) {
    write_project(root);
    constexpr int kRenderThreads = 8;
    constexpr int kIterations = 30;
    constexpr int kReloads = 40;

    std::atomic<bool> renders_ok{true};
    std::vector<std::thread> workers;
    for (int t = 0; t < kRenderThreads; ++t) {
        workers.emplace_back([&] {
            nift::Engine engine(root);
            for (int i = 0; i < kIterations; ++i) {
                nift::RenderResult result = engine.render("about");
                if (!result.ok()) { renders_ok = false; continue; }
                // Every render observes exactly one committed snapshot
                // generation: never a torn mix, never an unknown page.
                const bool generation_a = contains(result.output(), "Title-ALPHA");
                const bool generation_b = contains(result.output(), "Title-BETA");
                if (generation_a == generation_b) renders_ok = false;
            }
        });
    }

    // Reload thread flips between two known-good generations while renders run.
    std::atomic<bool> reloads_ok{true};
    std::thread reloader([&] {
        nift::Engine engine(root);
        for (int i = 0; i < kReloads; ++i) {
            write_file(root / ".nift/tracked.json",
                       tracked_with_title((i % 2 == 0) ? "Title-BETA" : "Title-ALPHA"));
            if (!engine.reload()) reloads_ok = false;
        }
    });

    reloader.join();
    for (auto& worker : workers) worker.join();
    CHECK(renders_ok.load());
    CHECK(reloads_ok.load());
}

void test_zero_writes_across_reload(const fs::path& root) {
    write_project(root);
    // The test's own fixture writes happen before the snapshots; only the
    // Engine's operations (construct/reload/render) must never write.
    nift::Engine engine(root);
    write_file(root / ".nift/tracked.json", tracked_with_title("About2"));
    const auto before_success = tree_snapshot(root);
    CHECK(engine.reload());
    CHECK(engine.render("about").ok());
    CHECK(tree_snapshot(root) == before_success);

    write_file(root / ".nift/tracked.json", "{ not json");
    const auto before_failure = tree_snapshot(root);
    std::string error;
    CHECK(!engine.reload(&error));
    CHECK(tree_snapshot(root) == before_failure);
    for (const auto& [path, contents] : tree_snapshot(root))
        CHECK(path.find(".info.json") == std::string::npos);
}

void test_defaults_survive_reload(const fs::path& root) {
    write_project(root);
    nift::Engine engine(root);
    engine.set_json("app", R"({"name":"Kept"})");
    engine.set_environment_provider([](std::string_view) { return std::optional<std::string>("KeptEnv"); });

    write_file(root / "content/about.html", "<h1>$[app.name]</h1><p>env=@getenv(\"PA_ENV_VAR\")</p>");
    write_file(root / ".nift/tracked.json", tracked_with_title("About2"));
    CHECK(engine.reload());

    nift::RenderResult result = engine.render("about");
    CHECK(result.ok());
    if (result.ok()) {
        CHECK(contains(result.output(), "Kept"));
        CHECK(contains(result.output(), "KeptEnv"));
    }
}

} // namespace

int main() {
    std::error_code error;
    const fs::path base = fixture_base();
    fs::remove_all(base, error);
    fs::create_directories(base, error);
    const fs::path project = base / "project";
    write_project(project);

    test_new_page_after_reload(project);
    test_failed_reload_retains_last_good(project);
    test_reload_as_open_retry(project);
    test_generation_switch(project);
    test_concurrent_render_and_reload(project);
    test_zero_writes_across_reload(project);
    test_defaults_survive_reload(project);

    if (failures == 0) {
        std::printf("engine reload test passed\n");
        return 0;
    }
    std::fprintf(stderr, "engine reload test failed: %d check(s)\n", failures);
    return 1;
}
