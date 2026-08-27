/* CP10: pure-C consumer proof for the Nift Embed C ABI.
 *
 * Compiled with a C compiler (not C++), so this proves the public header is
 * consumable from C and the static library links into a C program. Exercises
 * a text render, bindings, and the version query.
 */
#include <stdio.h>
#include <string.h>

#include "nift/c_abi.h"

static nift_status smoke_loader(void* user_data, const char* path, size_t path_len, nift_string* out);

int main(void) {
    if (strcmp(nift_abi_version(), NIFT_ABI_VERSION) != 0) return 1;
    if (nift_abi_version_major() != 1) return 1;

    nift_engine* engine = nift_engine_new();
    if (engine == NULL) return 1;

    const char* site = "hello";
    if (nift_engine_set_string(engine, "site", 4, site, 5) != NIFT_OK) return 1;

    nift_source page;
    page.kind = NIFT_SOURCE_TEXT;
    page.data = "site=$[site]";
    page.length = 12; /* strlen("site=$[site]") */
    page.logical_name = NULL;
    page.logical_name_length = 0;

    nift_source tpl;
    tpl.kind = NIFT_SOURCE_TEXT;
    tpl.data = "<main>@content</main>";
    tpl.length = 22; /* strlen("<main>@content</main>") */
    tpl.logical_name = NULL;
    tpl.logical_name_length = 0;

    nift_render_result* result = NULL;
    if (nift_engine_render(engine, &page, &tpl, NULL, &result) != NIFT_OK) return 1;
    if (nift_render_result_ok(result) != 1) return 1;

    nift_string output;
    if (nift_render_result_output(result, &output) != NIFT_OK) return 1;
    if (output.length != 24) return 1; /* strlen("<main>site=hello</main>") */
    if (strncmp(output.data, "<main>site=hello</main>", output.length) != 0) return 1;

    nift_render_result_free(result);

    /* Host failure with a supplied diagnostic must be preserved exactly. */
    engine = nift_engine_new();
    if (engine == NULL) return 1;
    if (nift_engine_set_loader(engine, smoke_loader, NULL) != NIFT_OK) return 1;
    page.kind = NIFT_SOURCE_PATH;
    page.data = "content/blog.html";
    page.length = 18;
    tpl.kind = NIFT_SOURCE_PATH;
    tpl.data = "templates/template.html";
    tpl.length = 25;
    result = NULL;
    if (nift_engine_render(engine, &page, &tpl, NULL, &result) != NIFT_OK) return 1;
    if (nift_render_result_ok(result) != 0) return 1;
    {
        nift_string err;
        if (nift_render_result_error_message(result, &err) != NIFT_OK) return 1;
        if (err.length != 14) return 1; /* strlen("loader exploded") */
        if (strncmp(err.data, "loader exploded", err.length) != 0) return 1;
    }
    nift_render_result_free(result);
    nift_engine_free(engine);

    /* CP19: distinct render_text / render_path entry points. */
    engine = nift_engine_new();
    if (engine == NULL) return 1;
    if (nift_engine_set_string(engine, "site", 4, "hello", 5) != NIFT_OK) return 1;
    result = NULL;
    if (nift_engine_render_text(engine, NULL, "site=$[site]", 12, &result) != NIFT_OK) return 1;
    if (nift_render_result_ok(result) != 1) return 1;
    if (nift_render_result_output(result, &output) != NIFT_OK) return 1;
    if (output.length != 10 || strncmp(output.data, "site=hello", 10) != 0) return 1;
    nift_render_result_free(result);

    /* render_path is ALWAYS a path: a missing file is a controlled error and
     * is never reinterpreted as literal template text. */
    result = NULL;
    if (nift_engine_render_path(engine, NULL, "no-such-file.html", 17, &result) != NIFT_OK) return 1;
    if (nift_render_result_ok(result) != 0) return 1;
    nift_render_result_free(result);
    nift_engine_free(engine);

    printf("C ABI C-consumer smoke passed\n");
    return 0;
}

static nift_status smoke_loader(void* user_data, const char* path, size_t path_len, nift_string* out) {
    const char* suffix;
    size_t suffix_len;
    (void)user_data;
    if (path_len >= 25 && strncmp(path + path_len - 25, "/templates/template.html", 25) == 0) {
        suffix = "<main>@content</main>";
        suffix_len = 20;
        out->data = suffix;
        out->length = suffix_len;
        return NIFT_OK;
    }
    out->data = "loader exploded";
    out->length = 14;
    return NIFT_ERROR_CALLBACK;
}
