// CP5: @pathto through the host path capability. The embedded engine computes
// relative paths from the per-render current output, applies the 404 rule when
// the page name is "404", treats every argument as a concrete project path (no
// fake tracked names), records requirements, and errors when there is no path
// context or the target does not exist.
#include "nift/nift.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "engine-pathto FAIL: %s (line %d)\n",       \
                         #cond, __LINE__);                                   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static bool contains(const std::vector<std::string>& items, const std::string& needle) {
    return std::find(items.begin(), items.end(), needle) != items.end();
}

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "nift-engine-pathto";
    fs::remove_all(root);
    fs::create_directories(root / "docs");
    fs::create_directories(root / "assets" / "css");
    auto write = [&](const fs::path& rel, const std::string& content) {
        std::ofstream f(root / rel);
        f << content;
    };
    write("index.html", "<h1>home</h1>");
    write("about.html", "<h1>about</h1>");
    write("404.html", "<h1>404</h1>");
    write("docs/page.html", "<h1>docs</h1>");
    write("assets/css/style.css", ".x{}");

    // 1. Relative path from the current output to a sibling page.
    {
        nift::Engine engine;
        engine.set_root(root);
        nift::Context context;
        context.set_current_output(root / "index.html");
        auto r = engine.render(nift::Source::path(root / "index.html"),
                               nift::Source::text("<a href=\"@pathto('about.html')\">A</a>@content"),
                               context);
        CHECK(r.ok());
        CHECK(r.output() == "<a href=\"./about.html\">A</a><h1>home</h1>");
    }

    // 2. Relative path up directories for a deeper current output.
    {
        nift::Engine engine;
        engine.set_root(root);
        nift::Context context;
        context.set_current_output(root / "docs" / "page.html");
        auto r = engine.render(nift::Source::path(root / "docs" / "page.html"),
                               nift::Source::text("<link href=\"@pathto('assets/css/style.css')\">@content"),
                               context);
        CHECK(r.ok());
        CHECK(r.output() == "<link href=\"../assets/css/style.css\"><h1>docs</h1>");
    }

    // 3. The 404 rule: page name "404" yields root-absolute web paths.
    {
        nift::Engine engine;
        engine.set_root(root);
        nift::Context context;
        context.set_page_name(std::string("404"));
        context.set_current_output(root / "404.html");
        auto r = engine.render(nift::Source::path(root / "404.html"),
                               nift::Source::text("<a href=\"@pathto('about.html')\">A</a>@content"),
                               context);
        CHECK(r.ok());
        CHECK(r.output() == "<a href=\"/about.html\">A</a><h1>404</h1>");
    }

    // 4. No path context => controlled error, not a guessed location.
    {
        nift::Engine engine;
        engine.set_root(root);
        auto r = engine.render(nift::Source::text("<p>p</p>"),
                               nift::Source::text("@pathto('about.html')@content"));
        CHECK(!r.ok());
        CHECK(r.error().message.find("path context") != std::string::npos);
    }

    // 5. Missing concrete target => controlled error.
    {
        nift::Engine engine;
        engine.set_root(root);
        nift::Context context;
        context.set_current_output(root / "index.html");
        auto r = engine.render(nift::Source::text("<p>p</p>"),
                               nift::Source::text("@pathto('missing.html')@content"), context);
        CHECK(!r.ok());
        CHECK(r.error().message.find("neither a tracked name nor a file that exists") != std::string::npos);
    }

    // 6. A bare name is treated as a concrete project path (no fake tracked
    //    pages in the embedded engine), so it errors when the file is absent.
    {
        nift::Engine engine;
        engine.set_root(root);
        nift::Context context;
        context.set_current_output(root / "index.html");
        auto r = engine.render(nift::Source::text("<p>p</p>"),
                               nift::Source::text("@pathto('about')@content"), context);
        CHECK(!r.ok());
    }

    // 7. Requirements are recorded for a resolved @pathto.
    {
        nift::Engine engine;
        engine.set_root(root);
        nift::Context context;
        context.set_current_output(root / "index.html");
        auto r = engine.render(nift::Source::path(root / "index.html"),
                               nift::Source::text("<a href=\"@pathto('about.html')\">A</a>@content"),
                               context);
        CHECK(r.ok());
        CHECK(contains(r.requirements(), "about.html"));
    }

    fs::remove_all(root);

    if (failures == 0) {
        std::printf("engine pathto test passed\n");
        return 0;
    }
    std::fprintf(stderr, "engine pathto test: %d failure(s)\n", failures);
    return 1;
}
