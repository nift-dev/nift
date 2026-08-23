// PA1 parity evidence: ProjectState (read-only project snapshot) must agree
// with ProjectInfo's read semantics exactly, must never write to disk, and must
// keep its shared read caches safe under concurrent readers.
#include "ProjectInfo.h"
#include "ProjectState.h"
#include "FileSystem.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
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

fs::path fixture_base() { return fs::current_path() / ".build" / "project-state-fixtures"; }

fs::path make_fixture(const std::string& name) {
    const fs::path root = fixture_base() / name;
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    return root;
}

void write_file(const fs::path& path, const std::string& contents) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

// Full recursive snapshot: relative path -> contents. Used to prove zero writes.
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

bool any_info_json(const std::map<std::string, std::string>& snapshot) {
    for (const auto& [path, contents] : snapshot)
        if (path.find(".info.json") != std::string::npos) return true;
    return false;
}

bool configs_equal(const Config& a, const Config& b) {
    return a.content_dir == b.content_dir && a.content_ext == b.content_ext &&
           a.output_dir == b.output_dir && a.output_ext == b.output_ext &&
           a.default_template == b.default_template && a.incremental_mode == b.incremental_mode &&
           a.build_threads == b.build_threads && a.minify_exts == b.minify_exts && a.contracts == b.contracts;
}

bool tracked_equal(const TrackedInfo& a, const TrackedInfo& b) {
    if (a.name != b.name || a.title != b.title || a.template_path != b.template_path ||
        a.content_ext != b.content_ext || a.output_ext != b.output_ext || a.minify != b.minify)
        return false;
    if (a.paginate.has_value() != b.paginate.has_value()) return false;
    if (a.paginate.has_value()) {
        if (a.paginate->items_per_page != b.paginate->items_per_page ||
            a.paginate->template_path != b.paginate->template_path ||
            a.paginate->separator_path != b.paginate->separator_path)
            return false;
    }
    return true;
}

const std::string kConfig = R"({
  "config": {
    "content-dir": "content/",
    "content-ext": ".html",
    "output-dir": "public/",
    "output-ext": ".html",
    "default-template": "templates/template.html",
    "incremental-mode": "modified",
    "build-threads": 4,
    "contracts": { "site": "content/site.json" },
    "minify-exts": [".css", ".js"]
  }
}
)";

const std::string kTracked = R"({
  "tracked": [
    { "name": "/", "title": "Home" },
    { "name": "about", "title": "About", "template": "templates/page.html" },
    { "name": "blog/", "title": "Blog",
      "paginate": { "items-per-page": 5, "template": "templates/blog.html", "separator": "templates/sep.html" } },
    { "name": "feed", "title": "Feed", "content-ext": ".xml", "output-ext": ".xml" },
    { "name": "scripts", "title": "Scripts", "content-ext": ".js", "output-ext": ".js", "minify": true }
  ]
}
)";

void write_valid_project(const fs::path& root) {
    write_file(root / ".nift/config.json", kConfig);
    write_file(root / ".nift/tracked.json", kTracked);
    for (const char* content : {"content/index.html", "content/about.html", "content/blog/index.html",
                                "content/feed.xml", "content/scripts.js"})
        write_file(root / content, "<p>content</p>");
    write_file(root / "content/site.json", R"({"site":{"name":"Nift"}})");
    for (const char* tpl : {"templates/template.html", "templates/page.html", "templates/blog.html",
                            "templates/sep.html"})
        write_file(root / tpl, "<html>@content</html>");
}

void test_valid_parity() {
    const fs::path root = make_fixture("valid");
    write_valid_project(root);

    ProjectInfo project;
    project.root = root;
    CHECK(project.load_config());
    CHECK(project.load_tracking());

    ProjectState state;
    std::string error;
    CHECK(state.open(root, error));

    CHECK(configs_equal(state.config(), project.config));
    CHECK(state.tracked().size() == project.tracked.size());
    CHECK(state.tracked().size() == 5);
    for (std::size_t i = 0; i < state.tracked().size(); ++i)
        CHECK(tracked_equal(state.tracked()[i], project.tracked[i]));

    for (const auto& info : project.tracked) {
        const TrackedInfo* mirrored = state.find(info.name);
        CHECK(mirrored != nullptr);
        if (mirrored) {
            CHECK(state.content_path(*mirrored) == project.content_path(info));
            CHECK(state.output_path(*mirrored) == project.output_path(info));
            CHECK(state.pagination_output_path(*mirrored, 1) == project.pagination_output_path(info, 1));
            CHECK(state.pagination_output_path(*mirrored, 3) == project.pagination_output_path(info, 3));
        }
    }
    CHECK(state.find("nope") == nullptr);
    CHECK(project.find("nope") == nullptr);
    CHECK(state.find("/") != nullptr);
    CHECK(state.relative(root / "content/about.html") == "content/about.html");

    // Hardcoded geometry spot-checks for the documented index/trailing-slash rules.
    const TrackedInfo* home = state.find("/");
    const TrackedInfo* blog = state.find("blog/");
    CHECK(home && state.content_path(*home) == root / "content/index.html");
    CHECK(home && state.output_path(*home) == root / "public/index.html");
    CHECK(blog && state.content_path(*blog) == root / "content/blog/index.html");
    CHECK(blog && state.pagination_output_path(*blog, 3) == root / "public/blog/3.html");
    const TrackedInfo* feed = state.find("feed");
    CHECK(feed && state.output_path(*feed) == root / "public/feed.xml");

    // Concurrent shared reads: find + geometry are immutable; the read caches
    // are mutex-protected. 8 threads hammer both and must observe stable data.
    constexpr int kThreads = 8;
    constexpr int kIterations = 50;
    std::atomic<bool> ok{true};
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < kIterations; ++i) {
                if (state.find("about") == nullptr) ok = false;
                const std::string* source = state.read_shared_source(root / "content/about.html");
                if (source == nullptr || *source != "<p>content</p>") ok = false;
                std::string json_error;
                auto doc = state.read_shared_json(root / "content/site.json", json_error);
                if (doc == nullptr) ok = false;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(ok.load());
}

struct InvalidCase {
    const char* name;
    const char* config;
    const char* tracked;  // nullptr means don't write tracked.json
};

void test_invalid_parity() {
    const std::vector<InvalidCase> cases = {
        // Config-stage rejections (tracked.json is valid/absent; config fails).
        {"unknown-key", R"({"config":{"bogus":1}})", kTracked.c_str()},
        {"non-string-field", R"({"config":{"content-dir":3}})", kTracked.c_str()},
        {"empty-content-dir", R"({"config":{"content-dir":""}})", kTracked.c_str()},
        {"bad-extension", R"({"config":{"content-ext":"html"}})", kTracked.c_str()},
        {"bad-build-threads", R"({"config":{"build-threads":1.5}})", kTracked.c_str()},
        {"bad-contract-name", R"({"config":{"contracts":{"9bad":"content/site.json"}}})", kTracked.c_str()},
        {"reserved-contract", R"({"config":{"contracts":{"title":"content/site.json"}}})", kTracked.c_str()},
        {"contract-outside-root", R"({"config":{"contracts":{"site":"../secret.json"}}})", kTracked.c_str()},
        {"contract-non-string", R"({"config":{"contracts":{"site":5}}})", kTracked.c_str()},
        {"minify-exts-not-array", R"({"config":{"minify-exts":"css"}})", kTracked.c_str()},
        {"minify-exts-unsupported", R"({"config":{"minify-exts":[".ts"]}})", kTracked.c_str()},
        {"bad-incremental-mode", R"({"config":{"incremental-mode":"wat"}})", kTracked.c_str()},
        {"config-not-object", R"({"config":[]})", kTracked.c_str()},
        {"no-config-member", R"({"settings":{}})", kTracked.c_str()},
        {"missing-config", nullptr, kTracked.c_str()},
        // Tracking-stage rejections (config is valid; tracked.json fails).
        {"missing-tracked", kConfig.c_str(), nullptr},
        {"malformed-tracked", kConfig.c_str(), "{ not json"},
        {"tracked-not-array", kConfig.c_str(), R"({"tracked":{"name":"x"}})"},
        {"entry-missing-name", kConfig.c_str(), R"({"tracked":[{"title":"X"}]})"},
        {"entry-non-string-title", kConfig.c_str(), R"({"tracked":[{"name":"a","title":3}]})"},
        {"bad-name-parent-component", kConfig.c_str(), R"({"tracked":[{"name":"../escape","title":"X"}]})"},
        {"bad-name-absolute", kConfig.c_str(), R"({"tracked":[{"name":"/abs","title":"X"}]})"},
        {"duplicate-name", kConfig.c_str(),
         R"({"tracked":[{"name":"about","title":"A"},{"name":"about","title":"B"}]})"},
        {"duplicate-path", kConfig.c_str(),
         R"({"tracked":[{"name":"/","title":"A"},{"name":"index","title":"B"}]})"},
        {"bad-extension-override", kConfig.c_str(),
         R"({"tracked":[{"name":"feed","title":"F","content-ext":"xml"}]})"},
        {"template-equals-content", kConfig.c_str(),
         R"({"tracked":[{"name":"about","title":"A","template":"content/about.html"}]})"},
        {"bad-paginate", kConfig.c_str(),
         R"({"tracked":[{"name":"blog/","title":"B","paginate":{"items-per-page":0}}]})"},
        {"paginate-non-int", kConfig.c_str(),
         R"({"tracked":[{"name":"blog/","title":"B","paginate":{"items-per-page":2.5}}]})"},
        {"minify-non-bool", kConfig.c_str(),
         R"({"tracked":[{"name":"scripts","title":"S","minify":"yes"}]})"},
    };

    for (const auto& test : cases) {
        const fs::path root = make_fixture(test.name);
        if (test.config != nullptr) write_file(root / ".nift/config.json", test.config);
        if (test.tracked != nullptr) write_file(root / ".nift/tracked.json", test.tracked);

        const auto before = tree_snapshot(root);

        ProjectInfo project;
        project.root = root;
        const bool project_ok = project.load_config() && project.load_tracking();

        ProjectState state;
        std::string error;
        const bool state_ok = state.open(root, error);

        // Parity: both must reject, and the embeddable error must be non-empty.
        CHECK(!state_ok);
        CHECK(!error.empty());
        CHECK(!project_ok);

        // Zero writes even on failure.
        CHECK(tree_snapshot(root) == before);
        if (tree_snapshot(root) != before)
            std::fprintf(stderr, "FAIL %s: ProjectState wrote to disk for case '%s'\n", __FILE__, test.name);
    }
}

void test_zero_writes() {
    const fs::path root = make_fixture("zero-writes");
    write_valid_project(root);

    const auto before = tree_snapshot(root);
    CHECK(!any_info_json(before));

    ProjectState state;
    std::string error;
    CHECK(state.open(root, error));
    for (const auto& info : state.tracked()) {
        state.content_path(info);
        state.output_path(info);
        state.pagination_output_path(info, 2);
        state.read_shared_source(state.content_path(info));
    }
    std::string json_error;
    state.read_shared_json(root / "content/site.json", json_error);

    const auto after = tree_snapshot(root);
    CHECK(after == before);
    CHECK(!any_info_json(after));
}

} // namespace

int main() {
    test_valid_parity();
    test_invalid_parity();
    test_zero_writes();

    if (failures == 0) {
        std::printf("project-state parity test passed\n");
        return 0;
    }
    std::fprintf(stderr, "project-state parity test failed: %d check(s)\n", failures);
    return 1;
}
