// CP19 canonical rendering API conformance:
//   render(name)          always a tracked project page name
//   render_path(path)     always a filesystem path
//   render_text(text)     always in-memory template source
// with the typed Source composition retained and no existence-based dispatch.
#include "nift/engine.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {
int g_failures = 0;
void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    } else {
        std::printf("PASS: %s\n", what);
    }
}
void write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path);
    out << content;
}
std::string make_project() {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / ("nift-cp19-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root / ".nift");
    fs::create_directories(root / "content");
    fs::create_directories(root / "content/products");
    fs::create_directories(root / "templates");
    fs::create_directories(root / "public");
    write_file(root / ".nift/config.json", R"({"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified"}})");
    write_file(root / ".nift/tracked.json", R"({"tracked":[{"name":"/","title":"Home","template":"templates/template.html"},{"name":"about","title":"About","template":"templates/template.html"},{"name":"products/headphones","title":"Headphones","template":"templates/template.html"}]})");
    write_file(root / "templates/template.html", "<main>@content</main>");
    write_file(root / "content/index.html", "<p>home</p>");
    write_file(root / "content/about.html", "<p>about</p>");
    write_file(root / "content/products/headphones.html", "<h1>$[product.name]</h1>");
    return root.string();
}
}  // namespace

int main() {
    using namespace nift;
    namespace fs = std::filesystem;
    const std::string project = make_project();
    Engine engine(project);
    check(engine.is_open(), "project opens");

    // 1. render(name) renders a tracked project page.
    {
        auto r = engine.render("about");
        check(r.ok() && r.output() == "<main><p>about</p></main>",
              "render(name) renders tracked page");
    }
    {
        Context ctx;
        ctx.set_json("product", R"({"name":"headphones"})");
        auto r = engine.render("products/headphones", ctx);
        check(r.ok() && r.output() == "<main><h1>headphones</h1></main>",
              "render(name, ctx) applies context bindings");
    }

    // 2. Unknown tracked name -> controlled unknown-page error (never treated
    //    as a path or as template text).
    {
        auto r = engine.render("no-such-page");
        check(!r.ok(), "unknown tracked page -> controlled error");
        check(r.output().empty(), "unknown tracked page yields no output");
    }

    // 3. render_path(existing) renders the file as a standalone partial.
    {
        fs::path fragment = fs::path(project) / "content/about.html";
        auto r = engine.render_path(fragment);
        check(r.ok() && r.output() == "<p>about</p>",
              "render_path(existing) renders the file");
    }

    // 4. render_path(missing) -> controlled missing-path error, never
    //    reinterpreted as literal text.
    {
        fs::path missing = fs::path(project) / "does-not-exist.html";
        auto r = engine.render_path(missing);
        check(!r.ok(), "render_path(missing) -> controlled error");
        check(r.output().empty(), "render_path(missing) yields no output");
    }

    // 5. render_text(text) renders the supplied bytes and never checks the
    //    filesystem: a string that names an existing file is still rendered as
    //    literal text.
    {
        auto r = engine.render_text("<p>literal</p>");
        check(r.ok() && r.output() == "<p>literal</p>", "render_text renders supplied bytes");
    }
    {
        std::string names_a_file = (fs::path(project) / "content/about.html").string();
        auto r = engine.render_text(names_a_file);
        check(r.ok() && r.output() == names_a_file,
              "render_text never resolves its argument as a file path");
    }

    // 6. Omitted context == fresh empty context; no request-state reuse. An
    //    unbound $[x] renders literally, so the no-context render proves no
    //    prior binding leaked into it.
    {
        auto a = engine.render_text("$[x]");
        Context ctx;
        ctx.set("x", std::string("value"));
        auto b = engine.render_text("$[x]", ctx);
        check(b.ok() && b.output() == "value", "context provided applies");
        check(a.ok() && a.output() == "$[x]",
              "no-context render sees no prior binding (renders literal)");
    }

    // 7. render_path / render_text with explicit context.
    {
        Context ctx;
        ctx.set_json("product", R"({"name":"studio"})");
        auto r = engine.render_path(fs::path(project) / "content/products/headphones.html", ctx);
        check(r.ok() && r.output() == "<h1>studio</h1>",
              "render_path(path, ctx) applies bindings");
    }
    {
        Context ctx;
        ctx.set("who", std::string("world"));
        auto r = engine.render_text("<p>$[who]</p>", ctx);
        check(r.ok() && r.output() == "<p>world</p>", "render_text(text, ctx) applies bindings");
    }

    // 8. Typed full composition still supports path/path, text/text, mixed.
    {
        auto r = engine.render(Source::path(fs::path(project) / "content/about.html"),
                               Source::path(fs::path(project) / "templates/template.html"));
        check(r.ok() && r.output() == "<main><p>about</p></main>",
              "composition path/path");
    }
    {
        auto r = engine.render(Source::text("<p>hi</p>"), Source::text("<main>@content</main>"));
        check(r.ok() && r.output() == "<main><p>hi</p></main>", "composition text/text");
    }
    {
        auto r = engine.render(Source::text("<p>mixed</p>"),
                               Source::path(fs::path(project) / "templates/template.html"));
        check(r.ok() && r.output() == "<main><p>mixed</p></main>", "composition text/path");
    }

    fs::remove_all(project);
    if (g_failures == 0) {
        std::printf("cp19 render API conformance: all passed\n");
        return 0;
    }
    std::printf("cp19 render API conformance: %d FAILURES\n", g_failures);
    return 1;
}