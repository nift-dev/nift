// PA2 evidence: ProjectHost adapts the validated ProjectState snapshot to the
// existing RenderHost seam, so the existing Parser/rendering core renders real
// Nift project pages with project-backed content/template/input loading, JSON,
// contracts, tracked output lookup, current-output @path geometry (including
// the 404 rule) and pagination - while writing nothing, making no build
// decisions, and keeping the concurrency contract.
#include "ProjectHost.h"
#include "ProjectInfoHost.h"
#include "RuntimeJson.h"
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
<p>home=@path("/")</p>
<p>blog=@path("blog/")</p>
<p>appjs=@path("public/app.js")</p>)";
const char* k404Content = R"(<p>home=@path("/")</p>
<p>blog=@path("blog/")</p>)";
const char* kBlogContent = R"(@json(d, 'data/items.json')
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

std::shared_ptr<const nift::RuntimeValue> parse_json(const char* text) {
    auto document = std::make_shared<json::Document>();
    std::string error;
    if (!json::Document::parse(text, *document, error)) {
        std::fprintf(stderr, "test JSON parse failed: %s\n", error.c_str());
        ++failures;
    }
    return std::make_shared<const nift::RuntimeValue>(nift::runtime_from_json(*document));
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

    const RenderHost::HostSource source = host.read_shared_source(state.root() / "content/about.html");
    CHECK(source.status == nift::HostStatus::Found && source.content != nullptr && contains(*source.content, "About"));
    std::string json_error;
    auto document = host.read_shared_json(state.root() / "content/site.json", json_error);
    CHECK(document != nullptr);
    auto runtime_document = host.read_shared_runtime_json(state.root() / "content/site.json", json_error);
    CHECK(runtime_document != nullptr && (*runtime_document)["name"].string == "Nift");
    CHECK(runtime_document == host.read_shared_runtime_json(state.root() / "content/site.json", json_error));
    nift::RuntimeValue environment;
    CHECK(host.environment_snapshot(environment, json_error));
    CHECK(environment.is_object());
    CHECK(host.source_exists(state.root() / "templates/template.html"));
    CHECK(!host.source_exists(state.root() / "templates/missing.html"));
    CHECK(host.source_readable(state.root() / "data/items.json"));
    CHECK(host.relative(state.root() / "content/about.html") == "content/about.html");
}

RenderResult render_page(const ProjectState& state, const char* name,
                         const std::unordered_map<std::string, std::shared_ptr<const nift::RuntimeValue>>& bindings) {
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
                           const std::unordered_map<std::string, std::shared_ptr<const nift::RuntimeValue>>& bindings) {
    // Home: template composition + @input partial + contract binding + title.
    RenderResult home = render_page(state, "/", bindings);
    CHECK(home.ok);
    if (home.ok) {
        CHECK(contains_all(home.output, {"<!doctype html>", "<title>Home</title>",
                                         R"(<meta name="site" content="Nift">)", "<h1>Home</h1>"}));
        CHECK(std::find(home.dependencies.begin(), home.dependencies.end(), std::string(".nift/config.json")) != home.dependencies.end());
        CHECK(std::find(home.dependencies.begin(), home.dependencies.end(), std::string("templates/head.html")) != home.dependencies.end());
    }

    // About: contract + host binding + tracked/concrete @path geometry.
    RenderResult about = render_page(state, "about", bindings);
    CHECK(about.ok);
    if (about.ok) {
        CHECK(contains_all(about.output, {"site=Nift", "app=TestApp", "home=./", "blog=blog/", "appjs=./app.js"}));
        CHECK(std::find(about.dependencies.begin(), about.dependencies.end(), std::string("content/site.json")) != about.dependencies.end());
        CHECK(std::find(about.dependencies.begin(), about.dependencies.end(), std::string("templates/page.html")) != about.dependencies.end());
    }

    // 404: tracked page whose @path is root-absolute for web serving.
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
                      const std::unordered_map<std::string, std::shared_ptr<const nift::RuntimeValue>>& bindings) {
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
                             const std::unordered_map<std::string, std::shared_ptr<const nift::RuntimeValue>>& bindings) {
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

void test_persistent_project_cache(const ProjectState& state) {
    ProjectInfo project;
    project.root = state.root();
    project.config = state.config();
    project.tracked = state.tracked();
    project.invalidate_tracked_index();
    ProjectInfoHost host(project);

    const auto first = *host.binding("project");
    CHECK(first && (*first)["files"].array.size() == state.tracked().size());
    nift::RuntimeValue hierarchy;
    std::string hierarchy_error;
    CHECK(host.resolve_page_member("about", "children", hierarchy, hierarchy_error));
    CHECK(hierarchy.is_array() && hierarchy.array.empty());

    TrackedInfo added;
    added.name = "about/post";
    added.title = "Persistent Added";
    project.tracked.push_back(added);
    project.invalidate_tracked_index();
    const auto after_track = *host.binding("project");
    CHECK(after_track && after_track != first);
    CHECK((*after_track)["files"].array.size() == state.tracked().size() + 1);
    CHECK(host.resolve_page_member("about", "children", hierarchy, hierarchy_error));
    CHECK(hierarchy.is_array() && hierarchy.array.size() == 1);
    if (hierarchy.is_array() && hierarchy.array.size() == 1)
        CHECK(hierarchy.array[0].string == ProjectInfoHost::page_ref_of("about/post"));

    project.tracked.pop_back();
    project.invalidate_tracked_index();
    const auto after_untrack = *host.binding("project");
    CHECK(after_untrack && after_untrack != after_track);
    CHECK((*after_untrack)["files"].array.size() == state.tracked().size());
    CHECK(host.resolve_page_member("about", "children", hierarchy, hierarchy_error));
    CHECK(hierarchy.is_array() && hierarchy.array.empty());

    CHECK(project.load_tracking());
    const auto after_reload = *host.binding("project");
    CHECK(after_reload && after_reload != after_untrack);
    CHECK((*after_reload)["files"].array.size() == state.tracked().size());

    write_file(project.root / ".nift/config.json",
        R"({"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified","build-threads":2,"contracts":{"site":"content/site.json"},"schemas":["model/site.schema"],"taxonomies":["model/site.tax"]}})");
    write_file(project.root / "model/site.schema",
        "@schema(post){\ntitle: string\ncategory: taxonomy(categories)\n}\n");
    write_file(project.root / "model/site.tax",
        "@taxonomy(categories){\nhierarchical: false\n}\n");
    write_file(project.root / "data/about.json",
        R"({"title":"About","category":"Guides/Start"})");
    CHECK(project.load_config());
    TrackedInfo* about = project.find("about");
    CHECK(about != nullptr);
    if (about) {
        about->type = "post";
        about->frontmatter = "data/about.json";
        project.invalidate_tracked_index();
    }
    const auto before_model_reload = *host.binding("project");
    CHECK(before_model_reload && (*before_model_reload)["schemas"]["post"]["fields"].array.size() == 2);
    CHECK(!(*before_model_reload)["taxonomy_info"]["categories"]["hierarchical"].boolean);
    CHECK((*before_model_reload)["taxonomies"]["categories"].array.size() == 1);
    CHECK(!(*before_model_reload)["taxonomies"]["categories"].array[0].has("parent"));

    write_file(project.root / "model/site.schema",
        "@schema(post){\ntitle: string\ncategory: taxonomy(categories)\nfeatured: bool = true\n}\n");
    write_file(project.root / "model/site.tax",
        "@taxonomy(categories){\nhierarchical: true\n}\n");
    CHECK(project.build_names({"about"}, true) == 0);
    const auto after_build = *host.binding("project");
    CHECK(after_build && after_build != before_model_reload);
    CHECK((*after_build)["files"].array.size() == state.tracked().size());
    CHECK((*after_build)["schemas"]["post"]["fields"].array.size() == 3);
    CHECK((*after_build)["taxonomy_info"]["categories"]["hierarchical"].boolean);
    CHECK((*after_build)["taxonomies"]["categories"].array[0]["parent"].string == "Guides");
}

} // namespace

int main() {
    const fs::path root = make_fixture();
    write_project(root);

    ProjectState state;
    std::string error;
    CHECK(state.open(root, error));
    if (!state.root().empty()) {
        const auto project_value = state.runtime_project_value();
        CHECK(project_value);
        CHECK(project_value == state.runtime_project_value());

        std::unordered_map<std::string, std::shared_ptr<const nift::RuntimeValue>> bindings;
        bindings["app"] = parse_json(R"({"name":"TestApp"})");

        test_host_capabilities(state);
        test_project_renders(state, bindings);
        test_zero_writes(state, bindings);
        test_concurrent_renders(state, bindings);
        test_persistent_project_cache(state);
    }

    if (failures == 0) {
        std::printf("project-host test passed\n");
        return 0;
    }
    std::fprintf(stderr, "project-host test failed: %d check(s)\n", failures);
    return 1;
}
