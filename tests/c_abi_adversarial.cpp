// CP10: C ABI adversarial / lifetime tests.
//
// Exercises ONLY the public C header (nift/c_abi.h): null/invalid arguments,
// empty/large/Unicode strings, malformed JSON, binding validation, host
// callbacks (user_data, NOT_FOUND, callback error), result lifetime across
// subsequent renders / reload / engine destruction, repeated create/destroy,
// concurrent renders, pagination/dependency/requirement iteration bounds, and
// the ABI version query. Undefined misuse (double-free, concurrent mutation)
// is documented, not detected.
#include "nift/c_abi.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

void write_file(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

std::string read_view(const nift_string& s) { return std::string(s.data, s.length); }

// Deterministic loader seam: serves fixed fixtures, records keys, honors a
// "fail" user_data marker.
struct LoaderState {
    std::vector<std::string> keys;
    bool fail = false;
};

nift_status test_loader(void* user_data, const char* path, size_t path_len, nift_string* out) {
    auto* state = static_cast<LoaderState*>(user_data);
    const std::string key(path, path_len);
    state->keys.push_back(key);
    if (state->fail) return NIFT_ERROR_CALLBACK;
    static const std::string tmpl = "<main>@content</main>\n";
    static const std::string content = "<p>LOADER-CONTENT</p>\n";
    if (key.find("/templates/template.html") != std::string::npos) { *out = {tmpl.data(), tmpl.size()}; return NIFT_OK; }
    if (key.find("/content/blog.html") != std::string::npos) { *out = {content.data(), content.size()}; return NIFT_OK; }
    return NIFT_ERROR_NOT_FOUND;
}

nift_status test_env(void* user_data, const char* name, size_t name_len, nift_string* out) {
    const std::string key(name, name_len);
    const auto* marker = static_cast<const std::string*>(user_data);
    if (key == *marker) { *out = {"alpha", 5}; return NIFT_OK; }
    return NIFT_ERROR_NOT_FOUND;
}

void test_version_and_handles() {
    CHECK(std::string(nift_abi_version()) == NIFT_ABI_VERSION);
    CHECK(nift_abi_version_major() == 1);
    CHECK(nift_abi_version_minor() == 0);

    nift_engine* engine = nift_engine_new();
    CHECK(engine != nullptr);
    CHECK(nift_engine_is_open(engine) == 0);

    // Null / invalid argument handling.
    nift_render_result* result = nullptr;
    CHECK(nift_engine_render(nullptr, nullptr, nullptr, nullptr, &result) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_engine_render(engine, nullptr, nullptr, nullptr, nullptr) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_engine_render_page(engine, nullptr, "p", 1, nullptr) == NIFT_ERROR_INVALID_ARGUMENT);
    nift_source bad{};
    bad.kind = NIFT_SOURCE_TEXT;
    bad.data = nullptr;
    bad.length = 1;  // NULL data with length > 0 is invalid
    nift_source empty_tmpl{};
    empty_tmpl.kind = NIFT_SOURCE_TEXT;
    CHECK(nift_engine_render(engine, &bad, &empty_tmpl, nullptr, &result) == NIFT_ERROR_INVALID_ARGUMENT);
    // NULL data with length 0 is the empty string (valid).
    nift_source empty{};
    empty.kind = NIFT_SOURCE_TEXT;
    CHECK(nift_engine_render(engine, &empty, &empty_tmpl, nullptr, &result) == NIFT_OK);
    CHECK(result != nullptr);
    CHECK(nift_render_result_ok(result) == 0);  // empty page is a controlled render error
    nift_string msg{};
    CHECK(nift_render_result_error_message(result, &msg) == NIFT_OK);
    nift_render_result_free(result);

    // Output accessor on a null result.
    nift_string out{};
    CHECK(nift_render_result_output(nullptr, &out) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_render_result_pagination_get(nullptr, 0, nullptr, nullptr) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_render_result_dependency_get(nullptr, 0, &out) == NIFT_ERROR_INVALID_ARGUMENT);

    nift_engine_free(engine);
}

void test_bindings_and_context() {
    nift_engine* engine = nift_engine_new();
    nift_context* context = nift_context_new();
    CHECK(engine && context);

    // Invalid binding names.
    CHECK(nift_engine_set_string(engine, "9bad", 4, "x", 1) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_engine_set_string(engine, "name", 4, "x", 1) == NIFT_ERROR_INVALID_ARGUMENT);  // structural built-in
    // Malformed JSON.
    CHECK(nift_engine_set_json(engine, "site", 4, "{not json", 9) == NIFT_ERROR_INVALID_ARGUMENT);
    // Valid bindings.
    CHECK(nift_engine_set_string(engine, "site", 4, "hello", 5) == NIFT_OK);
    CHECK(nift_engine_set_int(engine, "count", 5, 42) == NIFT_OK);
    CHECK(nift_engine_set_bool(engine, "flag", 4, 1) == NIFT_OK);
    CHECK(nift_engine_set_json(engine, "user", 4, "{\"name\":\"Acme\"}", 15) == NIFT_OK);
    CHECK(nift_context_set_string(context, "ctx", 3, "value", 5) == NIFT_OK);
    CHECK(nift_context_set_json(context, "obj", 3, "{\"a\":1}", 7) == NIFT_OK);
    CHECK(nift_context_set_title(context, "T", 1) == NIFT_OK);
    CHECK(nift_context_set_current_output(context, "out.html", 8) == NIFT_OK);
    CHECK(nift_context_set_page_name(context, "p", 1) == NIFT_OK);

    nift_string diag{};
    CHECK(nift_engine_open_error(engine, &diag) == NIFT_OK);
    CHECK(diag.length == 0);

    // Render using the engine default binding.
    nift_source page{};
    page.kind = NIFT_SOURCE_TEXT;
    const std::string page_text = "site=$[site] count=$[count] flag=$[flag] user=$[user.name] ctx=$[ctx]";
    page.data = page_text.data();
    page.length = page_text.size();
    nift_source tpl{};
    tpl.kind = NIFT_SOURCE_TEXT;
    const std::string tpl_text = "<main>@content</main>";
    tpl.data = tpl_text.data();
    tpl.length = tpl_text.size();
    nift_render_result* result = nullptr;
    CHECK(nift_engine_render(engine, &page, &tpl, context, &result) == NIFT_OK);
    CHECK(result != nullptr);
    CHECK(nift_render_result_ok(result) == 1);
    nift_string output{};
    CHECK(nift_render_result_output(result, &output) == NIFT_OK);
    const std::string rendered = read_view(output);
    CHECK(rendered == "<main>site=hello count=42 flag=true user=Acme ctx=value</main>");
    nift_render_result_free(result);

    // Result output stays valid after a subsequent render (own storage).
    nift_render_result* first = nullptr;
    CHECK(nift_engine_render(engine, &page, &tpl, context, &first) == NIFT_OK);
    nift_string first_out{};
    CHECK(nift_render_result_output(first, &first_out) == NIFT_OK);
    nift_render_result* second = nullptr;
    CHECK(nift_engine_render(engine, &page, &tpl, context, &second) == NIFT_OK);
    CHECK(nift_render_result_output(first, &first_out) == NIFT_OK);
    CHECK(std::string(first_out.data, first_out.length).find("hello") != std::string::npos);
    nift_render_result_free(first);
    nift_render_result_free(second);

    nift_engine_free(engine);
    nift_context_free(context);
}

void test_callbacks() {
    nift_engine* engine = nift_engine_new();
    CHECK(engine != nullptr);
    LoaderState loader_state;
    CHECK(nift_engine_set_loader(engine, test_loader, &loader_state) == NIFT_OK);

    // Loader seam: a path source resolves through the callback.
    nift_source page{};
    page.kind = NIFT_SOURCE_PATH;
    const std::string page_path = "content/blog.html";
    page.data = page_path.data();
    page.length = page_path.size();
    nift_source tpl{};
    tpl.kind = NIFT_SOURCE_PATH;
    const std::string tpl_path = "templates/template.html";
    tpl.data = tpl_path.data();
    tpl.length = tpl_path.size();
    nift_render_result* result = nullptr;
    CHECK(nift_engine_render(engine, &page, &tpl, nullptr, &result) == NIFT_OK);
    CHECK(nift_render_result_ok(result) == 1);
    nift_string output{};
    CHECK(nift_render_result_output(result, &output) == NIFT_OK);
    CHECK(read_view(output) == "<main><p>LOADER-CONTENT</p></main>\n");
    CHECK(loader_state.keys.size() >= 2);  // loader was invoked with user_data
    nift_render_result_free(result);

    // Loader miss -> controlled render failure (result not ok), not a crash.
    nift_source missing{};
    missing.kind = NIFT_SOURCE_PATH;
    const std::string missing_path = "content/nope.html";
    missing.data = missing_path.data();
    missing.length = missing_path.size();
    CHECK(nift_engine_render(engine, &missing, &tpl, nullptr, &result) == NIFT_OK);
    CHECK(nift_render_result_ok(result) == 0);
    nift_render_result_free(result);

    // Callback hard failure -> NIFT_ERROR_CALLBACK (not a render outcome).
    // Use a fresh engine so the source cache does not hide the loader call.
    nift_engine* failing = nift_engine_new();
    CHECK(failing != nullptr);
    LoaderState failing_state;
    failing_state.fail = true;
    CHECK(nift_engine_set_loader(failing, test_loader, &failing_state) == NIFT_OK);
    result = nullptr;
    CHECK(nift_engine_render(failing, &page, &tpl, nullptr, &result) == NIFT_ERROR_CALLBACK);
    CHECK(result == nullptr);
    nift_engine_free(failing);

    // Environment provider with user_data.
    std::string env_marker = "NIFT_ENV_A";
    CHECK(nift_engine_set_environment_provider(engine, test_env, &env_marker) == NIFT_OK);
    nift_source env_page{};
    env_page.kind = NIFT_SOURCE_TEXT;
    const std::string env_text = "@getenv(NIFT_ENV_A)|@getenv(NIFT_ENV_MISSING)";
    env_page.data = env_text.data();
    env_page.length = env_text.size();
    nift_source env_tpl{};
    env_tpl.kind = NIFT_SOURCE_TEXT;
    const std::string env_tpl_text = "<main>@content</main>";
    env_tpl.data = env_tpl_text.data();
    env_tpl.length = env_tpl_text.size();
    CHECK(nift_engine_render(engine, &env_page, &env_tpl, nullptr, &result) == NIFT_OK);
    CHECK(nift_render_result_ok(result) == 1);
    CHECK(nift_render_result_output(result, &output) == NIFT_OK);
    CHECK(read_view(output) == "<main>alpha|</main>");
    nift_render_result_free(result);

    nift_engine_free(engine);
}

fs::path make_pagination_project() {
    fs::path root = fs::temp_directory_path() / "nift-c-abi-pagination";
    fs::remove_all(root);
    write_file(root / ".nift/config.json",
               R"({"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","incremental-mode":"modified"}})");
    write_file(root / ".nift/tracked.json",
               R"({"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]})");
    write_file(root / "templates/template.html", "<main>$[title]</main>\n@content");
    write_file(root / "content/blog.html", "@item{one}@item{two}@item{three}@paginate");
    write_file(root / "content/blog.paginate.html",
               "<section>page $[paginate.current]/$[paginate.total]:[$[paginate.items]]</section>");
    return root;
}

void test_project_and_pagination() {
    const fs::path root = make_pagination_project();
    const std::string root_str = root.string();
    nift_engine* engine = nift_engine_open(root_str.data(), root_str.size());
    CHECK(engine != nullptr);
    CHECK(nift_engine_is_open(engine) == 1);

    nift_render_result* result = nullptr;
    CHECK(nift_engine_render_page(engine, nullptr, "blog", 4, &result) == NIFT_OK);
    CHECK(result != nullptr);
    CHECK(nift_render_result_ok(result) == 1);

    nift_string output{};
    CHECK(nift_render_result_output(result, &output) == NIFT_OK);
    CHECK(read_view(output) == "<main>Blog</main>\n<section>page 1/3:[one]</section>");

    CHECK(nift_render_result_pagination_count(result) == 2);
    unsigned int page = 0;
    nift_string page_out{};
    CHECK(nift_render_result_pagination_get(result, 0, &page, &page_out) == NIFT_OK);
    CHECK(page == 2);
    CHECK(read_view(page_out) == "<main>Blog</main>\n<section>page 2/3:[two]</section>");
    CHECK(nift_render_result_pagination_get(result, 1, &page, &page_out) == NIFT_OK);
    CHECK(page == 3);
    CHECK(read_view(page_out).find("[three]") != std::string::npos);
    // Bounds: index out of range.
    CHECK(nift_render_result_pagination_get(result, 2, &page, &page_out) == NIFT_ERROR_INVALID_ARGUMENT);

    CHECK(nift_render_result_dependency_count(result) == 3);
    nift_string dep{};
    CHECK(nift_render_result_dependency_get(result, 0, &dep) == NIFT_OK);
    CHECK(nift_render_result_dependency_get(result, 3, &dep) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_render_result_requirement_count(result) == 0);
    CHECK(nift_render_result_requirement_get(result, 0, &dep) == NIFT_ERROR_INVALID_ARGUMENT);

    // Unknown page -> controlled result error, not a mechanical failure.
    nift_render_result* err = nullptr;
    CHECK(nift_engine_render_page(engine, nullptr, "nope", 4, &err) == NIFT_OK);
    CHECK(err != nullptr);
    CHECK(nift_render_result_ok(err) == 0);
    nift_string err_msg{};
    CHECK(nift_render_result_error_message(err, &err_msg) == NIFT_OK);
    CHECK(read_view(err_msg).find("unknown page name") != std::string::npos);
    nift_render_result_free(err);

    // Result lifetime across a reload: the earlier result stays valid.
    nift_string out_before{};
    CHECK(nift_render_result_output(result, &out_before) == NIFT_OK);
    const std::string before = read_view(out_before);
    CHECK(nift_engine_reload(engine, nullptr) == NIFT_OK);
    CHECK(nift_render_result_output(result, &out_before) == NIFT_OK);
    CHECK(read_view(out_before) == before);

    // Result lifetime relative to engine destruction: result owns its storage.
    const std::string result_copy = before;
    nift_render_result* surviving = result;
    result = nullptr;
    nift_engine_free(engine);
    CHECK(nift_render_result_output(surviving, &out_before) == NIFT_OK);
    CHECK(read_view(out_before) == result_copy);
    nift_render_result_free(surviving);
    // Using a freed engine handle is undefined caller misuse, not a promised
    // detection; a NULL handle is the detectable case (covered above).
}

void test_repeated_create_destroy() {
    for (int i = 0; i < 100; ++i) {
        nift_engine* engine = nift_engine_new();
        nift_context* context = nift_context_new();
        CHECK(engine && context);
        nift_render_result* result = nullptr;
        nift_source page{};
        page.kind = NIFT_SOURCE_TEXT;
        const std::string text = "x";
        page.data = text.data();
        page.length = text.size();
        CHECK(nift_engine_render_partial(engine, &page, context, &result) == NIFT_OK);
        CHECK(nift_render_result_ok(result) == 1);
        nift_render_result_free(result);
        nift_context_free(context);
        nift_engine_free(engine);
    }
}

void test_large_and_unicode() {
    nift_engine* engine = nift_engine_new();
    nift_context* context = nift_context_new();
    std::string big(1 << 20, 'a');
    CHECK(nift_context_set_string(context, "big", 3, big.data(), big.size()) == NIFT_OK);
    nift_source page{};
    page.kind = NIFT_SOURCE_TEXT;
    const std::string page_text = "unicode=日本語 émoji 😀 $[big]";
    page.data = page_text.data();
    page.length = page_text.size();
    nift_source tpl{};
    tpl.kind = NIFT_SOURCE_TEXT;
    const std::string tpl_text = "<main>@content</main>";
    tpl.data = tpl_text.data();
    tpl.length = tpl_text.size();
    nift_render_result* result = nullptr;
    CHECK(nift_engine_render(engine, &page, &tpl, context, &result) == NIFT_OK);
    CHECK(nift_render_result_ok(result) == 1);
    nift_string output{};
    CHECK(nift_render_result_output(result, &output) == NIFT_OK);
    const std::string rendered = read_view(output);
    CHECK(rendered.find("日本語") != std::string::npos);
    CHECK(rendered.find("😀") != std::string::npos);
    // "$[big]" (6 bytes) resolves to the 1 MiB binding value.
    const std::size_t expected = std::string("<main>").size() + page_text.size() - 6 +
                                 big.size() + std::string("</main>").size();
    CHECK(rendered.size() == expected);
    CHECK(rendered.find(std::string(1 << 20, 'a')) != std::string::npos);
    nift_render_result_free(result);
    nift_context_free(context);
    nift_engine_free(engine);
}

void test_concurrent_renders() {
    nift_engine* engine = nift_engine_new();
    CHECK(engine != nullptr);
    std::atomic<bool> all_ok{true};
    std::vector<std::thread> workers;
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < 200; ++i) {
                nift_source page{};
                page.kind = NIFT_SOURCE_TEXT;
                const std::string text = "<h2>P</h2>";
                page.data = text.data();
                page.length = text.size();
                nift_source tpl{};
                tpl.kind = NIFT_SOURCE_TEXT;
                const std::string tpl_text = "<main>@content</main>";
                tpl.data = tpl_text.data();
                tpl.length = tpl_text.size();
                nift_render_result* result = nullptr;
                if (nift_engine_render(engine, &page, &tpl, nullptr, &result) != NIFT_OK) { all_ok = false; continue; }
                nift_string output{};
                if (nift_render_result_output(result, &output) != NIFT_OK ||
                    std::string(output.data, output.length) != "<main><h2>P</h2></main>")
                    all_ok = false;
                nift_render_result_free(result);
            }
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(all_ok.load());
    nift_engine_free(engine);
}

}  // namespace

int main() {
    test_version_and_handles();
    test_bindings_and_context();
    test_callbacks();
    test_project_and_pagination();
    test_repeated_create_destroy();
    test_large_and_unicode();
    test_concurrent_renders();

    if (failures == 0) {
        std::printf("C ABI adversarial test passed\n");
        return 0;
    }
    std::fprintf(stderr, "C ABI adversarial test failed: %d check(s)\n", failures);
    return 1;
}
