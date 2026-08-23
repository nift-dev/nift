// PA2 evidence: ProjectHost adapts the validated ProjectState snapshot to the
// existing RenderHost seam, so the existing Parser/rendering core renders real
// Nift project pages with project-backed content/template/input loading, JSON,
// contracts, tracked output lookup, current-output @pathto geometry (including
// the 404 rule) and pagination - while writing nothing, making no build
// decisions, and keeping the concurrency contract.
#include "ProjectHost.h"
#include "ProjectState.h"
#include "FileSystem.h"
#include "Json.h"
#include "Parser.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
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

const char* kConfig = R"({"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified","build-threads":2,"contracts":{"site":"content/site.json"}}})";
const char* kTracked = R"({"tracked":[
 {"name":"/","title":"Home","template":"templates/template.html"},
 {"name":"about","title":"About","template":"templates/page.html"},
 {"name":"404","title":"Not Found","template":"templates/page.html"},
 {"name":"blog/","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":2}}
]})";

const char* kTemplateHtml = R"(<!doctype html><title>$[title]</title><head>@input("head.html")</head><body>@content</body>)";
const char* kHeadHtml = R"(<meta name="site" content="$[site.name]">)";
const char* kPageHtml = R"(<main>@content</main>)";
const char* kAboutContent = R"(<h1>About</h1>
<p>site=$[site.name]</p>
<p>app=$[app.name]</p>
<p>home=@pathto("/")</p>
<p>blog=@pathto("blog/")</p>
<p>appjs=@pathto("public/app.js")</p>)";
const char* k404Content = R"(<p>home=@pathto("/")</p>
<p>blog=@pathto("blog/")</p>)";
const char* kBlogContent = R"(@json('data/items.json', d)
@for(x : d.items){@item{$[x.name]}}
@paginate)";
const char* kPaginateHtml = R"(<div class="page-$[paginate.current]">$[paginate.items]</div>)";

fs::path fixture_base() { return fs::current_path() / ".build" / "project-host-fixtures"; }

fs::path make_fixture() {
    const fs::path root = fixture_base() / "project";
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

void write_project(const fs::path& root) {
    write_file(root / ".nift/config.json", kConfig);
    write_file(root / ".nift/tracked.json", kTracked);
    write_file(root / "templates/template.html", kTemplateHtml);
    write_file(root / "templates/head.html", kHeadHtml);
    write_file(root / "templates/page.html", kPageHtml);
    write_file(root / "content/index.html", "<h1>Home</h1>");
    write_file(root / "content/about.html", kAboutContent);
    write_file(root / "content/404.html", k404Content);
    write_file(root / "content/blog/index.html", kBlogContent);
    write_file(root / "content/blog/index.paginate.html", kPaginateHtml);
    write_file(root / "content/site.json", R"({"name":"Nift"})");
    write_file(root / "data/items.json", R"({"items":[{"name":"one"},{"name":"two"},{"name":"three"},{"name":"four"},{"name":"five"}]})");
    write_file(root / "public/app.js", "console.log('x');");
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

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool contains_all(const std::string& haystack, std::initializer_list<const char*> needles) {
    for (const char* needle : needles)
        if (haystack.find(needle) == std::string::npos) return false;
    return true;
}

std::shared_ptr<const json::Document> parse_json(const char* text) {
    auto document = std::make_shared<json::Document>();
    std::string error;
    if (!json::Document::parse(text, *document, error)) {
        std::fprintf(stderr, "test JSON parse failed: %s\n", error.c_str());
        ++failures;
    }
    return document;
}

void test_host_capabilities(const ProjectState& state) {
    ProjectHost host(state);

    CHECK(host.has_output_context());
    CHECK(host.root() == state.root());
    CHECK(host.output_dir() == "public/");
    CHECK(host.build_threads() == 2);
    CHECK(host.is_contract_name("site"));
    CHECK(!host.is_contract_name("nope"));
    const std::string* contract = host.contract_source("site");
    CHECK(contract != nullptr && *contract == "content/site.json");
    CHECK(host.contract_source("nope") == nullptr);

    const auto about = host.tracked_output_path("about");
    CHECK(about.has_value() && about->path == state.root() / "public/about.html" && !about->index_page);
    const auto blog = host.tracked_output_path("blog/");
    CHECK(blog.has_value() && blog->path == state.root() / "public/blog/index.html" && blog->index_page);
    const auto root_path = host.tracked_output_path("/");
    CHECK(root_path.has_value() && root_path->path == state.root() / "public/index.html" && root_path->index_page);
    CHECK(!host.tracked_output_path("unknown").has_value());

    const std::string* source = host.read_shared_source(state.root() / "content/about.html");
    CHECK(source != nullptr && contains(*source, "About"));
    std::string json_error;
    auto document = host.read_shared_json(state.root() / "content/site.json", json_error);
    CHECK(document != nullptr);
    CHECK(host.source_exists(state.root() / "templates/template.html"));
    CHECK(!host.source_exists(state.root() / "templates/missing.html"));
    CHECK(host.source_readable(state.root() / "data/items.json"));
    CHECK(host.relative(state.root() / "content/about.html") == "content/about.html");
}

RenderResult render_page(const ProjectState& state, const char* name,
                         const std::unordered_map<std::string, std::shared_ptr<const json::Document>>& bindings) {
    const TrackedInfo* tracked = state.find(name);
    if (tracked == nullptr) {
        std::fprintf(stderr, "render_page: no tracked page '%s'\n", name);
        ++failures;
        return RenderResult{};
    }
    TrackedInfo info = *tracked;
    ProjectHost host(state, &bindings);
    Parser parser(host, info);
    RenderResult result = parser.render();
    if (!result.ok)
        std::fprintf(stderr, "render_page('%s') error: %s (source=%s line=%zu)\n", name,
                     result.error.message.c_str(), result.error.source_file.c_str(), result.error.line);
    return result;
}

void test_project_renders(const ProjectState& state,
                          const std::unordered_map<std::string, std::shared_ptr<const json::Document>>& bindings) {
    // Home: template composition + @input partial + contract binding + title.
    RenderResult home = render_page(state, "/", bindings);
    CHECK(home.ok);
    if (home.ok) {
        CHECK(contains_all(home.output, {"<!doctype html>", "<title>Home</title>",
                                         R"(<meta name="site" content="Nift">)", "<h1>Home</h1>"}));
        CHECK(std::find(home.dependencies.begin(), home.dependencies.end(), std::string(".nift/config.json")) != home.dependencies.end());
        CHECK(std::find(home.dependencies.begin(), home.dependencies.end(), std::string("templates/head.html")) != home.dependencies.end());
    }

    // About: contract + host binding + tracked/concrete @pathto geometry.
    RenderResult about = render_page(state, "about", bindings);
    CHECK(about.ok);
    if (about.ok) {
        CHECK(contains_all(about.output, {"site=Nift", "app=TestApp", "home=./", "blog=blog/", "appjs=./app.js"}));
        CHECK(std::find(about.dependencies.begin(), about.dependencies.end(), std::string("content/site.json")) != about.dependencies.end());
        CHECK(std::find(about.dependencies.begin(), about.dependencies.end(), std::string("templates/page.html")) != about.dependencies.end());
    }

    // 404: tracked page whose @pathto is root-absolute for web serving.
    RenderResult not_found = render_page(state, "404", bindings);
    CHECK(not_found.ok);
    if (not_found.ok) CHECK(contains_all(not_found.output, {"home=/", "blog=/blog/"}));

    // Paginated page: @json source, item collection, per-page outputs with the
    // correct project geometry for each page.
    RenderResult blog = render_page(state, "blog/", bindings);
    CHECK(blog.ok);
    if (blog.ok) {
        CHECK(blog.pagination_outputs.size() == 3);
        CHECK(blog.output == blog.pagination_outputs.front());
        if (blog.pagination_outputs.size() == 3) {
            CHECK(contains_all(blog.pagination_outputs[0], {"class=\"page-1\"", "onetwo"}));
            CHECK(contains_all(blog.pagination_outputs[1], {"class=\"page-2\"", "threefour"}));
            CHECK(contains_all(blog.pagination_outputs[2], {"class=\"page-3\"", "five"}));
        }
        CHECK(std::find(blog.dependencies.begin(), blog.dependencies.end(), std::string("data/items.json")) != blog.dependencies.end());
        CHECK(std::find(blog.dependencies.begin(), blog.dependencies.end(), std::string("content/blog/index.paginate.html")) != blog.dependencies.end());
    }

    // Unknown page names must not exist in the snapshot registry.
    CHECK(state.find("nope") == nullptr);
}

void test_zero_writes(const ProjectState& state,
                      const std::unordered_map<std::string, std::shared_ptr<const json::Document>>& bindings) {
    const fs::path root = state.root();
    const auto before = tree_snapshot(root);
    for (const char* name : {"/", "about", "404", "blog/"}) {
        RenderResult result = render_page(state, name, bindings);
        CHECK(result.ok);
    }
    const auto after = tree_snapshot(root);
    CHECK(after == before);
    for (const auto& [path, contents] : after)
        CHECK(path.find(".info.json") == std::string::npos);
}

void test_concurrent_renders(const ProjectState& state,
                             const std::unordered_map<std::string, std::shared_ptr<const json::Document>>& bindings) {
    constexpr int kThreads = 8;
    constexpr int kIterations = 20;
    std::atomic<bool> ok{true};
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < kIterations; ++i) {
                RenderResult about = render_page(state, "about", bindings);
                if (!about.ok || about.output.find("app=TestApp") == std::string::npos) ok = false;
                RenderResult blog = render_page(state, "blog/", bindings);
                if (!blog.ok || blog.pagination_outputs.size() != 3) ok = false;
                RenderResult home = render_page(state, "/", bindings);
                if (!home.ok || home.output.find("<h1>Home</h1>") == std::string::npos) ok = false;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(ok.load());
}

} // namespace

int main() {
    const fs::path root = make_fixture();
    write_project(root);

    ProjectState state;
    std::string error;
    CHECK(state.open(root, error));
    if (!state.root().empty()) {
        std::unordered_map<std::string, std::shared_ptr<const json::Document>> bindings;
        bindings["app"] = parse_json(R"({"name":"TestApp"})");

        test_host_capabilities(state);
        test_project_renders(state, bindings);
        test_zero_writes(state, bindings);
        test_concurrent_renders(state, bindings);
    }

    if (failures == 0) {
        std::printf("project-host test passed\n");
        return 0;
    }
    std::fprintf(stderr, "project-host test failed: %d check(s)\n", failures);
    return 1;
}
