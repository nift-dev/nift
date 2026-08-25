// CP10.2: Embed host-seam failure contract (C++ Engine level).
//
// The environment/loader provider contract is value / absent / error
// (nift::HostResult). A host ERROR travels through the render computation
// itself: the RenderResult fails with the diagnostic, identically for
// callbacks on the render's calling thread and on the pagination worker
// threads. NotFound is the ordinary "unset" case; Found with an empty value is
// "present but empty".
#include "nift/engine.h"
#include "nift/context.h"
#include "nift/source.h"

// Internal headers for the parser-level separator host-error tests (the
// built-in project host reads the pagination separator from disk, so the
// HostResult::Error path needs a custom host to exercise deterministically).
#include "Parser.h"
#include "ProjectHost.h"
#include "ProjectState.h"
#include "Types.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

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

void write_file(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

// Two paginated pages: blog's paginate template reads @getenv(FAIL_BARRIER)
// and other's reads @getenv(OK_BARRIER), so a provider can fail one page and
// satisfy the other deterministically even though the pagination page loop
// runs on Engine-owned worker threads.
fs::path make_paginated_project() {
    fs::path root = fs::temp_directory_path() / "nift-host-seam-pg";
    fs::remove_all(root);
    write_file(root / ".nift/config.json",
               R"({"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}})");
    write_file(root / ".nift/tracked.json",
               R"({"tracked":[
 {"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}},
 {"name":"other","title":"Other","template":"templates/template.html","paginate":{"items-per-page":1}}
]})");
    write_file(root / "templates/template.html", "<main>$[title]</main>\n@content");
    write_file(root / "content/blog.html", "@item{one}@item{two}@item{three}@paginate");
    write_file(root / "content/other.html", "@item{a}@item{b}@paginate");
    // @getenv in the paginate templates forces the pagination page loop to
    // invoke the environment provider on its worker threads.
    write_file(root / "content/blog.paginate.html",
               "<section>@getenv(FAIL_BARRIER) page $[paginate.current]/$[paginate.total]</section>");
    write_file(root / "content/other.paginate.html",
               "<section>@getenv(OK_BARRIER) page $[paginate.current]/$[paginate.total]</section>");
    return root;
}

nift::HostResult test_provider(std::string_view name) {
    if (name == "FAIL_BARRIER") return {nift::HostStatus::Error, "", "host exploded"};
    if (name == "OK_BARRIER") return {nift::HostStatus::Found, "ok", ""};
    return {nift::HostStatus::NotFound, "", ""};
}

void test_standalone_env_failure() {
    nift::Engine engine;
    engine.set_environment_provider([](std::string_view name) -> nift::HostResult {
        if (name == "FAIL") return {nift::HostStatus::Error, "", "host exploded"};
        if (name == "EMPTY") return {nift::HostStatus::Found, "", ""};
        return {nift::HostStatus::NotFound, "", ""};
    });
    nift::Source page(nift::Source::text("@getenv(FAIL)"));
    nift::Source tpl(nift::Source::text("<main>@content</main>"));

    // Hard failure -> controlled render failure.
    nift::RenderResult failed = engine.render(page, tpl);
    CHECK(!failed.ok());
    CHECK(failed.error().message.find("host exploded") != std::string::npos);

    // NotFound -> ordinary unset, renders empty.
    nift::Source unset(nift::Source::text("@getenv(MISSING)"));
    nift::RenderResult unset_result = engine.render(unset, tpl);
    CHECK(unset_result.ok());
    CHECK(unset_result.output() == "<main></main>");

    // Found with empty value -> present but empty (same output, distinct status).
    nift::Source empty(nift::Source::text("@getenv(EMPTY)"));
    nift::RenderResult empty_result = engine.render(empty, tpl);
    CHECK(empty_result.ok());
    CHECK(empty_result.output() == "<main></main>");
}

void test_paginated_env_failure_on_worker() {
    const fs::path root = make_paginated_project();
    nift::Engine engine(root);
    CHECK(engine.is_open());
    engine.set_environment_provider(test_provider);

    // blog's paginate template reads @getenv(FAIL_BARRIER); the pagination
    // page loop runs on Engine-owned worker threads, and the env failure there
    // must fail the overall RenderResult (no silent "unset").
    nift::RenderResult result = engine.render("blog");
    CHECK(!result.ok());
    CHECK(result.error().message.find("host exploded") != std::string::npos);

    // other's paginate template reads @getenv(OK_BARRIER); it renders and
    // paginates normally on the same worker-thread path.
    nift::RenderResult ok_result = engine.render("other");
    CHECK(ok_result.ok());
    CHECK(ok_result.pagination().size() == 1);
    CHECK(ok_result.output().find("ok") != std::string::npos);

    // NotFound on the worker -> ordinary unset semantics, render succeeds.
    nift::Engine unset_engine(root);
    unset_engine.set_environment_provider([](std::string_view name) -> nift::HostResult {
        return {nift::HostStatus::NotFound, "", ""};
    });
    nift::RenderResult unset_result = unset_engine.render("blog");
    CHECK(unset_result.ok());
    CHECK(unset_result.pagination().size() == 2);
}

void test_paginated_env_failure_concurrent_attribution() {
    const fs::path root = make_paginated_project();
    // One engine, two concurrent paginated renders. blog hits the failing
    // env; other hits the succeeding env. The host-error path travels through
    // the worker threads into each render's own result: blog fails, other
    // succeeds, and no render steals the other's callback status.
    nift::Engine engine(root);
    CHECK(engine.is_open());
    engine.set_environment_provider(test_provider);

    for (int order = 0; order < 2; ++order) {
        nift::RenderResult failing_result, ok_result;
        std::thread failing_thread;
        std::thread ok_thread;
        if (order == 0) {
            failing_thread = std::thread([&] { failing_result = engine.render("blog"); });
            ok_thread = std::thread([&] { ok_result = engine.render("other"); });
        } else {
            ok_thread = std::thread([&] { ok_result = engine.render("other"); });
            failing_thread = std::thread([&] { failing_result = engine.render("blog"); });
        }
        failing_thread.join();
        ok_thread.join();
        CHECK(!failing_result.ok());
        CHECK(failing_result.error().message.find("host exploded") != std::string::npos);
        CHECK(ok_result.ok());
        CHECK(ok_result.output().find("ok") != std::string::npos);
    }
}

// A host that reports a deterministic HostStatus for the pagination separator
// path and otherwise reads through the project host (disk).
class SeparatorProbeHost : public ProjectHost {
public:
    SeparatorProbeHost(const ProjectState& state, nift::HostStatus separator_status,
                       std::string separator_error)
        : ProjectHost(state, nullptr, nullptr),
          separator_status_(separator_status),
          separator_error_(std::move(separator_error)) {}

    HostSource read_shared_source(const fs::path& path) const override {
        if (path.filename().string().find(".separator.html") != std::string::npos) {
            if (separator_status_ == nift::HostStatus::Error)
                return {nift::HostStatus::Error, nullptr, separator_error_};
            if (separator_status_ == nift::HostStatus::NotFound)
                return {nift::HostStatus::NotFound, nullptr, ""};
            static const std::string empty;
            return {nift::HostStatus::Found, &empty, ""};
        }
        return ProjectHost::read_shared_source(path);
    }

private:
    nift::HostStatus separator_status_;
    std::string separator_error_;
};

// Pagination separator: NotFound -> no separator (optional in ABSENCE only);
// Found empty -> valid empty separator; Error -> the render FAILS with the
// host diagnostic (Error != NotFound).
void test_pagination_separator_host_error() {
    const fs::path root = fs::temp_directory_path() / "nift-host-seam-sep";
    fs::remove_all(root);
    write_file(root / ".nift/config.json",
               R"({"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}})");
    write_file(root / ".nift/tracked.json",
               R"({"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]})");
    write_file(root / "templates/template.html", "<main>$[title]</main>\n@content");
    write_file(root / "content/blog.html", "@item{one}@item{two}@paginate");
    write_file(root / "content/blog.paginate.html",
               "<section>$[paginate.items]-$[paginate.current]/$[paginate.total]</section>");
    write_file(root / "content/blog.separator.html", "--sep--");

    ProjectState state;
    std::string open_error;
    CHECK(state.open(root, open_error));
    TrackedInfo info = *state.find("blog");

    // Error -> render fails with the host diagnostic.
    {
        SeparatorProbeHost host(state, nift::HostStatus::Error, "separator backend failed");
        Parser parser(host, info);
        RenderResult result = parser.render();
        CHECK(!result.ok);
        CHECK(result.error.message.find("separator backend failed") != std::string::npos);
    }
    // NotFound -> no separator; render succeeds.
    {
        SeparatorProbeHost host(state, nift::HostStatus::NotFound, "");
        Parser parser(host, info);
        RenderResult result = parser.render();
        CHECK(result.ok);
        CHECK(result.output.find("--sep--") == std::string::npos);
    }
    // Found empty -> valid empty separator; render succeeds.
    {
        SeparatorProbeHost host(state, nift::HostStatus::Found, "");
        Parser parser(host, info);
        RenderResult result = parser.render();
        CHECK(result.ok);
        CHECK(result.output.find("--sep--") == std::string::npos);
    }
}

// A host that probes the pagination-template path deterministically.
class PaginationTemplateProbeHost : public ProjectHost {
public:
    PaginationTemplateProbeHost(const ProjectState& state, nift::HostStatus status,
                                std::string error)
        : ProjectHost(state, nullptr, nullptr), status_(status), error_(std::move(error)) {}

    HostSource read_shared_source(const fs::path& path) const override {
        if (path.filename().string().find(".paginate.html") != std::string::npos) {
            if (status_ == nift::HostStatus::Error)
                return {nift::HostStatus::Error, nullptr, error_};
            if (status_ == nift::HostStatus::NotFound)
                return {nift::HostStatus::NotFound, nullptr, ""};
            static const std::string tmpl("<section>$[paginate.items]</section>");
            return {nift::HostStatus::Found, &tmpl, ""};
        }
        return ProjectHost::read_shared_source(path);
    }

private:
    nift::HostStatus status_;
    std::string error_;
};

// Pagination-template host-error parity: Error must preserve the host
// diagnostic; NotFound keeps the canonical missing/unreadable message.
void test_pagination_template_host_error() {
    const fs::path root = fs::temp_directory_path() / "nift-host-seam-pgtmpl";
    fs::remove_all(root);
    write_file(root / ".nift/config.json",
               R"({"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}})");
    write_file(root / ".nift/tracked.json",
               R"({"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]})");
    write_file(root / "templates/template.html", "<main>$[title]</main>\n@content");
    write_file(root / "content/blog.html", "@item{one}@item{two}@paginate");
    write_file(root / "content/blog.paginate.html",
               "<section>$[paginate.items]</section>");

    ProjectState state;
    std::string open_error;
    CHECK(state.open(root, open_error));
    TrackedInfo info = *state.find("blog");

    // Error -> the host diagnostic survives.
    PaginationTemplateProbeHost error_host(state, nift::HostStatus::Error, "pagination template backend failed");
    {
        Parser parser(error_host, info);
        RenderResult result = parser.render();
        CHECK(!result.ok);
        CHECK(result.error.message.find("pagination template backend failed") != std::string::npos);
    }
    // NotFound -> canonical missing/unreadable pagination-template diagnostic.
    PaginationTemplateProbeHost notfound_host(state, nift::HostStatus::NotFound, "");
    {
        Parser parser(notfound_host, info);
        RenderResult result = parser.render();
        CHECK(!result.ok);
        CHECK(result.error.message.find("pagination template is missing, unreadable, or outside the project") != std::string::npos);
    }
}

// Deterministic multi-page error selection: with a single build worker the
// pagination page loop renders pages in page order, and the first failing page
// in page order determines the returned diagnostic. This freezes the public
// page-order rule for multiple failures; it does NOT by itself prove
// completion-order independence under multiple concurrent workers (the
// implementation aggregates page results by page index, so that property is
// deferred to the broader concurrency/hardening campaign).
void test_pagination_error_selection_order() {
    const fs::path root = fs::temp_directory_path() / "nift-host-seam-order";
    fs::remove_all(root);
    write_file(root / ".nift/config.json",
               R"({"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}})");
    write_file(root / ".nift/tracked.json",
               R"({"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]})");
    write_file(root / "templates/template.html", "<main>$[title]</main>\n@content");
    write_file(root / "content/blog.html", "@item{one}@item{two}@paginate");
    write_file(root / "content/blog.paginate.html",
               "<section>@getenv(FAIL) $[paginate.current]</section>");
    nift::Engine engine(root);
    CHECK(engine.is_open());
    std::atomic<int> calls{0};
    engine.set_environment_provider([&](std::string_view) -> nift::HostResult {
        const int call = calls.fetch_add(1);
        return {nift::HostStatus::Error, "",
                call == 0 ? "error-one" : "error-two"};
    });
    nift::RenderResult result = engine.render("blog");
    CHECK(!result.ok());
    // Page 1 is the first failing page in page order -> "error-one".
    CHECK(result.error().message.find("error-one") != std::string::npos);
    CHECK(result.error().message.find("error-two") == std::string::npos);
}

}  // namespace

int main() {
    test_standalone_env_failure();
    test_paginated_env_failure_on_worker();
    test_paginated_env_failure_concurrent_attribution();
    test_pagination_separator_host_error();
    test_pagination_template_host_error();
    test_pagination_error_selection_order();

    if (failures == 0) {
        std::printf("host seam test passed\n");
        return 0;
    }
    std::fprintf(stderr, "host seam test failed: %d check(s)\n", failures);
    return 1;
}
