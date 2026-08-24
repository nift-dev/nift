// Checked-read semantics (performance-repair regression): read_shared_source
// distinguishes a valid empty file from missing/unreadable/non-regular input,
// so @content/@input/render classify the read failure instead of treating it
// as empty content, and a failed read is never cached as a valid empty source.
#include "nift/nift.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "engine-source-read FAIL: %s (line %d)\n",    \
                         #cond, __LINE__);                                     \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "nift-engine-source-read";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "templates");
    std::filesystem::create_directories(root / "content");

    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    const auto render = [&](const std::string& tpl, const std::string& page) -> nift::RenderResult {
        nift::Engine engine;
        engine.set_root(root);
        nift::Context context;
        return engine.render(nift::Source::path(page), nift::Source::path(tpl), context);
    };

    // 1. A valid empty file is read as empty content, not as a read failure.
    //    1a. an empty page composed through a template renders empty;
    //    1b. an empty template rendered as a partial renders empty.
    {
        write(root / "templates/t.html", "@content");
        write(root / "content/page.html", "");
        nift::RenderResult r = render("templates/t.html", "content/page.html");
        CHECK(r.ok());
        if (r.ok()) CHECK(r.output().empty());

        write(root / "templates/empty.html", "");
        nift::Engine engine;
        engine.set_root(root);
        nift::RenderResult partial =
            engine.render(nift::Source::path("templates/empty.html"), nift::Context{});
        CHECK(partial.ok());
        if (partial.ok()) CHECK(partial.output().empty());
    }

    // 2. A missing template is a typed failure ("template file is not
    //    readable"), not an empty render.
    {
        write(root / "content/page.html", "<p>x</p>");
        nift::RenderResult r = render("templates/missing.html", "content/page.html");
        CHECK(!r.ok());
        CHECK(contains(r.error().message, "template file is not readable"));
    }

    // 3. A missing content page is a typed failure ("content file is not
    //    readable"), matching the missing-source reject class.
    {
        write(root / "templates/t.html", "@content");
        nift::RenderResult r = render("templates/t.html", "content/gone.html");
        CHECK(!r.ok());
        CHECK(contains(r.error().message, "content file is not readable"));
    }

    // 4. A directory used as a template is "not readable" (non-regular input).
    {
        write(root / "content/page.html", "<p>x</p>");
        std::filesystem::create_directories(root / "templates/asdir");
        nift::RenderResult r = render("templates/asdir", "content/page.html");
        CHECK(!r.ok());
        CHECK(contains(r.error().message, "template file is not readable"));
        std::filesystem::remove_all(root / "templates/asdir");
    }

    // 5. A failed read is not cached as valid empty content: a missing page
    //    errors, then creating the page makes a later render succeed.
    {
        write(root / "templates/t.html", "@content");
        nift::RenderResult first = render("templates/t.html", "content/later.html");
        CHECK(!first.ok());
        write(root / "content/later.html", "<p>now</p>");
        nift::RenderResult second = render("templates/t.html", "content/later.html");
        CHECK(second.ok());
        if (second.ok()) CHECK(second.output().find("<p>now</p>") != std::string::npos);
    }

    // 6. Unreadable regular file (permission denied) is a typed failure where
    //    the platform/privilege permits. Skipped when running as root, where a
    //    chmod 000 file is still readable.
    {
        const std::string page = (root / "content/locked.html").string();
        write(root / "content/locked.html", "<p>locked</p>");
        ::chmod(page.c_str(), 0);
        nift::RenderResult r = render("templates/t.html", "content/locked.html");
        ::chmod(page.c_str(), 0644);
        if (::geteuid() != 0) {
            CHECK(!r.ok());
            CHECK(contains(r.error().message, "content file is not readable"));
        } else {
            // As root the file is readable, so the render succeeds; either way
            // the read is consistent (never a bogus empty).
            CHECK(r.ok());
        }
    }

    if (failures == 0) {
        std::printf("engine source-read test passed\n");
        return 0;
    }
    std::fprintf(stderr, "engine source-read test failed: %d check(s)\n", failures);
    return 1;
}
