// PA3 evidence: the public project-aware Engine API. Explicit project
// construction loads a validated immutable snapshot; render("page-name") drives
// the shared core through ProjectHost exactly like the CLI; Engine defaults,
// Context overlays and the environment provider retain their precedence; a
// failed project open or unknown page name is a controlled error; dependencies
// and requirements are surfaced on the public result; renders never write.
#include "nift/nift.h"

#include "FileSystem.h"

#include <algorithm>
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

fs::path fixture_base() { return fs::current_path() / ".build" / "engine-project-fixtures"; }

void write_project(const fs::path& root) {
    write_file(root / ".nift/config.json",
               R"({"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified","build-threads":2,"contracts":{"site":"content/site.json"}}})");
    write_file(root / ".nift/tracked.json",
               R"({"tracked":[
 {"name":"/","title":"Home","template":"templates/template.html"},
 {"name":"about","title":"About","template":"templates/page.html"},
 {"name":"blog/","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":2}}
]})");
    write_file(root / "templates/template.html",
               R"(<!doctype html><title>$[title]</title><head>@input("head.html")</head><body>@content</body>)");
    write_file(root / "templates/head.html", R"(<meta name="site" content="$[site.name]">)");
    write_file(root / "templates/page.html", R"(<main>@content</main>)");
    write_file(root / "content/index.html", "<h1>Home</h1>");
    write_file(root / "content/about.html",
               R"(<h1>About</h1>
<p>site=$[site.name]</p>
<p>app=$[app.name]</p>
<p>env=@getenv("PA_ENV_VAR")</p>
<p>home=@pathto("/")</p>)");
    write_file(root / "content/blog/index.html",
               R"(@json(d, 'data/items.json')
@for(x : d.items){@item{$[x.name]}}
@paginate)");
    write_file(root / "content/blog/index.paginate.html",
               R"(<div class="page-$[paginate.current]">$[paginate.items]</div>)");
    write_file(root / "content/site.json", R"({"name":"Nift"})");
    write_file(root / "data/items.json",
               R"({"items":[{"name":"one"},{"name":"two"},{"name":"three"},{"name":"four"},{"name":"five"}]})");
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

void test_open_and_defaults(const fs::path& root) {
    nift::Engine engine(root);
    CHECK(engine.is_open());
    CHECK(engine.open_error().empty());

    engine.set_json("app", R"({"name":"DefaultApp"})");
    nift::RenderResult about = engine.render("about");
    CHECK(about.ok());
    if (about.ok()) {
        CHECK(contains(about.output(), "app=DefaultApp"));
        CHECK(contains(about.output(), "site=Nift"));
        CHECK(contains(about.output(), "home=./"));
        // @pathto emits a requirement (the destination relative path) that the
        // public result must not discard.
        CHECK(std::find(about.requirements().begin(), about.requirements().end(), std::string("public/index.html")) != about.requirements().end());
        CHECK(std::find(about.dependencies().begin(), about.dependencies().end(), std::string("content/site.json")) != about.dependencies().end());
    }

    nift::RenderResult home = engine.render("/");
    CHECK(home.ok());
    if (home.ok()) {
        CHECK(contains(home.output(), "<title>Home</title>"));
        CHECK(contains(home.output(), R"(<meta name="site" content="Nift">)"));
        CHECK(contains(home.output(), "<h1>Home</h1>"));
    }
}

void test_context_overlay_and_environment(const fs::path& root) {
    nift::Engine engine(root);
    engine.set_json("app", R"({"name":"DefaultApp"})");
    engine.set_environment_provider([](std::string_view) { return std::optional<std::string>("ProviderVal"); });

    nift::Context context;
    context.set_json("app", R"({"name":"OverlayApp"})");
    nift::RenderResult about = engine.render("about", context);
    CHECK(about.ok());
    if (about.ok()) {
        CHECK(contains(about.output(), "app=OverlayApp"));
        CHECK(contains(about.output(), "env=ProviderVal"));
    }

    // The page-name argument is authoritative: a page_name set on the Context
    // must not redirect the render.
    nift::Context misleading;
    misleading.set_page_name("nope");
    misleading.set_json("app", R"({"name":"StillAbout"})");
    nift::RenderResult about_again = engine.render("about", misleading);
    CHECK(about_again.ok());
    if (about_again.ok()) CHECK(contains(about_again.output(), "app=StillAbout"));

    // Title overlay: Context title wins over the tracked title.
    nift::Context titled;
    titled.set_title("Custom Title");
    nift::RenderResult home = engine.render("/", titled);
    CHECK(home.ok());
    if (home.ok()) CHECK(contains(home.output(), "<title>Custom Title</title>"));
}

void test_host_binding_vs_contract(const fs::path& root) {
    // The fixture's config declares a contract "site" -> content/site.json
    // {"name":"Nift"}. A host-supplied binding (Engine default) with the same
    // name must precede the contract binding: the parser's established seam
    // (host bindings -> @json -> contracts) is now concrete in project mode.
    nift::Engine engine(root);
    engine.set_json("site", R"({"name":"HostWins"})");
    nift::RenderResult result = engine.render("about");
    CHECK(result.ok());
    if (result.ok()) CHECK(contains(result.output(), "site=HostWins"));

    // Without the host binding the contract binding is visible.
    nift::Engine plain(root);
    nift::RenderResult contract_result = plain.render("about");
    CHECK(contract_result.ok());
    if (contract_result.ok()) CHECK(contains(contract_result.output(), "site=Nift"));
}

void test_unknown_page_and_control_flow(const fs::path& root) {
    nift::Engine engine(root);
    nift::RenderResult unknown = engine.render("nope");
    CHECK(!unknown.ok());
    CHECK(contains(unknown.error().message, "unknown page name 'nope'"));
    CHECK(unknown.output().empty());
}

void test_failed_open_controlled(const fs::path& root) {
    // Non-project root.
    {
        nift::Engine engine(root / "does-not-exist");
        CHECK(!engine.is_open());
        CHECK(!engine.open_error().empty());
        nift::RenderResult result = engine.render("about");
        CHECK(!result.ok());
        CHECK(!result.error().message.empty());
        CHECK(result.error().message == engine.open_error());
    }
    // Invalid project config.
    {
        const fs::path bad = root / "invalid-config";
        std::error_code error;
        fs::create_directories(bad / ".nift", error);
        write_file(bad / ".nift/config.json", R"({"config":{"bogus":1}})");
        write_file(bad / ".nift/tracked.json", R"({"tracked":[]})");
        nift::Engine engine(bad);
        CHECK(!engine.is_open());
        CHECK(contains(engine.open_error(), "unknown config key 'bogus'"));
        CHECK(!engine.render("about").ok());
    }
    // Valid config but malformed tracking.
    {
        const fs::path bad = root / "invalid-tracking";
        std::error_code error;
        fs::create_directories(bad / ".nift", error);
        write_file(bad / ".nift/config.json",
                   R"({"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified"}})");
        write_file(bad / ".nift/tracked.json", "{ not json");
        nift::Engine engine(bad);
        CHECK(!engine.is_open());
        CHECK(contains(engine.open_error(), "invalid tracked.json"));
        CHECK(!engine.render("about").ok());
    }
}

void test_pagination_through_public_api(const fs::path& root) {
    nift::Engine engine(root);
    nift::RenderResult blog = engine.render("blog/");
    CHECK(blog.ok());
    if (blog.ok()) {
        // CP8: the public render exposes the complete pagination set -- output
        // is always page 1 and pagination() carries pages 2..N ascending with
        // page numbers and per-page rendered output.
        CHECK(contains(blog.output(), "class=\"page-1\""));
        CHECK(contains(blog.output(), "onetwo"));
        CHECK(std::find(blog.dependencies().begin(), blog.dependencies().end(), std::string("data/items.json")) != blog.dependencies().end());
        CHECK(std::find(blog.dependencies().begin(), blog.dependencies().end(), std::string("content/blog/index.paginate.html")) != blog.dependencies().end());

        const auto& pages = blog.pagination();
        CHECK(pages.size() == 2);
        if (pages.size() == 2) {
            CHECK(pages[0].page == 2);
            CHECK(pages[1].page == 3);
            CHECK(contains(pages[0].output, "class=\"page-2\""));
            CHECK(contains(pages[0].output, "threefour"));
            CHECK(contains(pages[1].output, "class=\"page-3\""));
            CHECK(contains(pages[1].output, "five"));
        }

        // Non-paginated pages expose an empty pagination set.
        nift::RenderResult about = engine.render("about");
        CHECK(about.ok());
        CHECK(about.pagination().empty());
    }
}

void test_zero_writes(const fs::path& root) {
    const auto before = tree_snapshot(root);
    nift::Engine engine(root);
    engine.set_json("app", R"({"name":"DefaultApp"})");
    nift::RenderResult a = engine.render("about");
    nift::RenderResult b = engine.render("blog/");
    nift::RenderResult c = engine.render("/");
    CHECK(a.ok() && b.ok() && c.ok());
    const auto after = tree_snapshot(root);
    CHECK(after == before);
    for (const auto& [path, contents] : after)
        CHECK(path.find(".info.json") == std::string::npos);
}

void test_concurrent_project_renders(const fs::path& root) {
    constexpr int kThreads = 8;
    constexpr int kIterations = 20;
    std::atomic<bool> ok{true};
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            nift::Engine engine(root);
            engine.set_json("app", R"({"name":"DefaultApp"})");
            for (int i = 0; i < kIterations; ++i) {
                nift::RenderResult about = engine.render("about");
                if (!about.ok() || !contains(about.output(), "app=DefaultApp")) ok = false;
                nift::RenderResult blog = engine.render("blog/");
                if (!blog.ok() || !contains(blog.output(), "page-1")) ok = false;
                nift::Context context;
                context.set_json("app", R"({"name":"Ctx"})");
                nift::RenderResult overlaid = engine.render("about", context);
                if (!overlaid.ok() || !contains(overlaid.output(), "app=Ctx")) ok = false;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(ok.load());
}

void test_standalone_unchanged() {
    // Default Engine() remains deterministic standalone (no project discovery).
    nift::Engine standalone;
    nift::RenderResult result = standalone.render(nift::Source::text("$[greeting]"), nift::Source::text("<b>@content</b>"));
    (void)result.ok();
    CHECK(!standalone.is_open());
}

} // namespace

int main() {
    std::error_code error;
    const fs::path base = fixture_base();
    fs::remove_all(base, error);
    fs::create_directories(base, error);
    const fs::path project = base / "project";
    write_project(project);

    test_open_and_defaults(project);
    test_context_overlay_and_environment(project);
    test_host_binding_vs_contract(project);
    test_unknown_page_and_control_flow(project);
    test_failed_open_controlled(base);
    test_pagination_through_public_api(project);
    test_zero_writes(project);
    test_concurrent_project_renders(project);
    test_standalone_unchanged();

    if (failures == 0) {
        std::printf("engine project test passed\n");
        return 0;
    }
    std::fprintf(stderr, "engine project test failed: %d check(s)\n", failures);
    return 1;
}
