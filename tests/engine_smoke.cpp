// Direct C++ tests for the Embedded Nift public rendering seam (CP2).
// Exercises the same parser/evaluator the CLI uses, without a Nift project.
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
            std::fprintf(stderr, "engine-smoke FAIL: %s (line %d)\n", #cond, \
                         __LINE__);                                          \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static bool contains(const std::vector<std::string>& items, const std::string& needle) {
    return std::find(items.begin(), items.end(), needle) != items.end();
}

int main() {
    namespace fs = std::filesystem;

    nift::Engine engine;

    // 1. Standalone partial (in-memory text).
    {
        auto r = engine.render(nift::Source::text("<nav>Home</nav>"));
        CHECK(r.ok());
        CHECK(r.output() == "<nav>Home</nav>");
    }

    // 2. Page + template composition (both in-memory).
    {
        auto r = engine.render(nift::Source::text("<main>hello</main>"),
                               nift::Source::text("<html><body>@content</body></html>"));
        CHECK(r.ok());
        CHECK(r.output() == "<html><body><main>hello</main></body></html>");
    }

    // 3. Exactly-one-@content: zero and two must both fail.
    {
        auto r = engine.render(nift::Source::text("<p>page</p>"),
                               nift::Source::text("<html>no content slot</html>"));
        CHECK(!r.ok());
        CHECK(r.error().message.find("@content") != std::string::npos);
    }
    {
        auto r = engine.render(nift::Source::text("<p>page</p>"),
                               nift::Source::text("<html>@content @content</html>"));
        CHECK(!r.ok());
        CHECK(r.error().message.find("@content") != std::string::npos);
    }

    // 4. @content in a standalone partial is an error (no content slot).
    {
        auto r = engine.render(nift::Source::text("<div>@content</div>"));
        CHECK(!r.ok());
        CHECK(r.error().message.find("@content") != std::string::npos);
    }

    // 5. Missing path source is a controlled error.
    {
        auto r = engine.render(nift::Source::path("/nonexistent-nift-cp2/page.html"),
                               nift::Source::text("<html>@content</html>"));
        CHECK(!r.ok());
    }

    // Fixture files for path/mixed sources and @input.
    const fs::path tmp = fs::temp_directory_path() / "nift-engine-cp2";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    const fs::path page_file = tmp / "page.html";
    const fs::path template_file = tmp / "tpl.html";
    const fs::path nav_file = tmp / "nav.html";
    {
        std::ofstream f(page_file);
        f << "<h1>page</h1>";
    }
    {
        std::ofstream f(template_file);
        f << "<html>@content</html>";
    }
    {
        std::ofstream f(nav_file);
        f << "<nav>N</nav>";
    }

    // 6. Mixed path/text sources.
    {
        auto r = engine.render(nift::Source::path(page_file),
                               nift::Source::text("<html>@content</html>"));
        CHECK(r.ok());
        CHECK(r.output() == "<html><h1>page</h1></html>");
    }
    {
        auto r = engine.render(nift::Source::text("<h1>page</h1>"),
                               nift::Source::path(template_file));
        CHECK(r.ok());
        CHECK(r.output() == "<html><h1>page</h1></html>");
    }

    // 7. Dependency discovery: path sources are reported.
    {
        auto r = engine.render(nift::Source::path(page_file), nift::Source::path(template_file));
        CHECK(r.ok());
        CHECK(r.output() == "<html><h1>page</h1></html>");
        const auto& deps = r.dependencies();
        CHECK(contains(deps, page_file.generic_string()));
        CHECK(contains(deps, template_file.generic_string()));
    }

    // 8. Path partial + dependency.
    {
        auto r = engine.render(nift::Source::path(nav_file));
        CHECK(r.ok());
        CHECK(r.output() == "<nav>N</nav>");
        CHECK(contains(r.dependencies(), nav_file.generic_string()));
    }

    // 9. Root resolves relative path sources and @input; dependencies are
    //    spelled relative to root.
    {
        nift::Engine rooted;
        rooted.set_root(tmp);
        auto r = rooted.render(nift::Source::path("page.html"),
                               nift::Source::text("<html>@content</html>"));
        CHECK(r.ok());
        CHECK(r.output() == "<html><h1>page</h1></html>");
        CHECK(contains(r.dependencies(), "page.html"));
    }
    {
        nift::Engine rooted;
        rooted.set_root(tmp);
        auto r = rooted.render(nift::Source::text("<main>m</main>"),
                               nift::Source::text("<body>@input(\"nav.html\")@content</body>"));
        CHECK(r.ok());
        CHECK(r.output() == "<body><nav>N</nav><main>m</main></body>");
        CHECK(contains(r.dependencies(), "nav.html"));
    }

    // 10. Relative @input without a root must error, never resolve to cwd.
    {
        auto r = engine.render(nift::Source::text("<main>m</main>"),
                               nift::Source::text("@input(\"nav.html\")@content"));
        CHECK(!r.ok());
        CHECK(r.error().message.find("@input") != std::string::npos);
    }

    // 11. Per-render Context supplies page identity (title metadata).
    {
        nift::Context context;
        context.set_title("Dashboard");
        auto r = engine.render(nift::Source::text("<h1>hi</h1>"),
                               nift::Source::text("<title>$[title]</title>@content"),
                               context);
        CHECK(r.ok());
        CHECK(r.output() == "<title>Dashboard</title><h1>hi</h1>");
    }

    fs::remove_all(tmp);

    if (failures == 0) {
        std::printf("engine smoke test passed\n");
        return 0;
    }
    std::fprintf(stderr, "engine smoke test: %d failure(s)\n", failures);
    return 1;
}
