/* CP10: pure-C consumer proof for the Nift Embed C ABI.
 *
 * Compiled with a C compiler (not C++), so this proves the public header is
 * consumable from C and the static library links into a C program. Exercises
 * a text render, bindings, and the version query.
 */
#include <stdio.h>
#include <string.h>

#include "nift/c_abi.h"

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
    nift_engine_free(engine);

    printf("C ABI C-consumer smoke passed\n");
    return 0;
}
