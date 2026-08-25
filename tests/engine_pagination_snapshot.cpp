// CP8.1 evidence: the complete pagination set of one RenderResult comes from a
// single immutable Engine snapshot, including while reload() publishes a new
// generation concurrently, and a failed reload retains the last known-good
// pagination generation.
//
// Deterministic interleave (no sleeps): the pagination template renders
// @getenv("BARRIER") on every page, so the environment provider callback fires
// during pagination assembly -- after the render has captured its project
// snapshot and before the complete multi-page RenderResult has been assembled.
// The provider blocks on a condition variable; the test thread observes
// "entered", reloads generation B, then releases the barrier. Every page also
// renders $[title] (a snapshot value), so a result that mixed generations
// would show GEN-A on some pages and GEN-B on others. The single result must
// be entirely GEN-A and the next render entirely GEN-B.
#include "nift/nift.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
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
    // Failure to flush/close would silently leave a partial fixture; surface it.
    out.flush();
    if (!out.good()) {
        std::fprintf(stderr, "FAIL %s:%d: could not write %s\n", __FILE__, __LINE__, path.c_str());
        std::exit(1);
    }
}

fs::path fixture_base() { return fs::current_path() / ".build" / "engine-pagination-snapshot-fixtures"; }

const char* kConfig = R"({"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified"}})";

std::string tracked_with_title(const char* title) {
    return std::string(R"({"tracked":[{"name":"blog","title":")") + title +
           R"(","template":"templates/template.html","paginate":{"items-per-page":1}}]})";
}

void write_project(const fs::path& root) {
    write_file(root / ".nift/config.json", kConfig);
    write_file(root / ".nift/tracked.json", tracked_with_title("GEN-A"));
    write_file(root / "templates/template.html", "<main>$[title]</main>\n@content");
    write_file(root / "content/blog.html", "@item{A1}@item{A2}@item{A3}@paginate");
    write_file(root / "content/blog.paginate.html",
               "<section>@getenv(\"BARRIER\")$[title] page $[paginate.current]/$[paginate.total]:[$[paginate.items]]</section>");
}

// Every page -- page 1 and each pagination page -- must come from generation
// `title`, never mixed. Each page renders $[title] independently in its
// paginate body, so a torn/mixed result is directly observable per page.
bool result_is_entirely(nift::RenderResult& result, const std::string& generation,
                        const std::string& other_generation, bool paginated) {
    if (!result.ok()) return false;
    if (!contains(result.output(), generation)) return false;
    if (contains(result.output(), other_generation)) return false;
    for (const auto& page : result.pagination()) {
        if (!contains(page.output, generation)) return false;
        if (contains(page.output, other_generation)) return false;
    }
    if (paginated) {
        if (result.pagination().size() != 2) return false;
        if (result.pagination()[0].page != 2) return false;
        if (result.pagination()[1].page != 3) return false;
    } else {
        if (!result.pagination().empty()) return false;
    }
    return true;
}

// Deterministic concurrent-reload snapshot test. The environment provider
// blocks on the first @getenv("BARRIER") inside the pagination page loop;
// reload(B) is issued while the render is mid-assembly; the released render
// must still be entirely generation A and the next render entirely B.
void test_concurrent_reload_single_snapshot(const fs::path& root) {
    write_project(root);
    nift::Engine engine(root);
    CHECK(engine.is_open());

    std::mutex barrier_mutex;
    std::condition_variable barrier_cv;
    bool entered = false;
    bool release = false;
    std::atomic<int> armed{1};

    engine.set_environment_provider([&](std::string_view name) -> std::optional<std::string> {
        if (name == "BARRIER" && armed.exchange(0)) {
            std::unique_lock<std::mutex> lock(barrier_mutex);
            entered = true;
            barrier_cv.notify_all();
            barrier_cv.wait(lock, [&] { return release; });
        }
        return std::nullopt;
    });

    nift::RenderResult result;
    std::thread renderer([&] {
        result = engine.render("blog");
    });

    // Wait until the render is deterministically inside pagination assembly.
    {
        std::unique_lock<std::mutex> lock(barrier_mutex);
        barrier_cv.wait(lock, [&] { return entered; });
    }

    // Publish generation B while the render is mid-flight.
    write_file(root / ".nift/tracked.json", tracked_with_title("GEN-B"));
    std::string reload_error;
    CHECK(engine.reload(&reload_error));

    // Release the render.
    {
        std::lock_guard<std::mutex> lock(barrier_mutex);
        release = true;
    }
    barrier_cv.notify_all();
    renderer.join();

    // The single result is entirely generation A -- page 1 and pages 2..3.
    CHECK(result_is_entirely(result, "GEN-A", "GEN-B", true));

    // The next render observes the newly published generation B.
    nift::RenderResult next = engine.render("blog");
    CHECK(result_is_entirely(next, "GEN-B", "GEN-A", true));
}

// Failed reload retains the last known-good pagination generation.
void test_failed_reload_retains_last_good(const fs::path& root) {
    write_project(root);  // title = GEN-A
    nift::Engine engine(root);
    CHECK(engine.is_open());

    nift::RenderResult first = engine.render("blog");
    CHECK(result_is_entirely(first, "GEN-A", "GEN-B", true));

    // Disk becomes invalid; reload must fail and retain generation A.
    write_file(root / ".nift/tracked.json", "{ not json");
    std::string reload_error;
    CHECK(!engine.reload(&reload_error));
    CHECK(!reload_error.empty());
    CHECK(engine.is_open());

    nift::RenderResult after_failed = engine.render("blog");
    CHECK(result_is_entirely(after_failed, "GEN-A", "GEN-B", true));

    // A later valid reload recovers; the next render observes the new generation.
    write_file(root / ".nift/tracked.json", tracked_with_title("GEN-C"));
    CHECK(engine.reload(&reload_error));
    nift::RenderResult recovered = engine.render("blog");
    CHECK(result_is_entirely(recovered, "GEN-C", "GEN-A", true));
}

}  // namespace

int main() {
    const fs::path base = fixture_base();
    fs::remove_all(base);

    test_concurrent_reload_single_snapshot(base / "concurrent");
    test_failed_reload_retains_last_good(base / "failed");

    if (failures == 0) {
        std::printf("engine pagination snapshot test passed\n");
        return 0;
    }
    std::fprintf(stderr, "engine pagination snapshot test failed: %d check(s)\n", failures);
    return 1;
}
