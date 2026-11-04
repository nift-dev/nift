/* Tooling checkpoint (component 6): an external consumer of libnift_c.so.
 *
 * tests/abi/external_consumer.sh copies this file, the public header and the
 * built shared library into a directory outside the repository, compiles it
 * there with the C compiler against the copied header only, links it
 * dynamically (-lnift_c, no static archive, no repository object) and runs it.
 * It calls the supported embedding API end to end through the dynamic symbol
 * table: version, engine and bindings, context, render (page and template),
 * partial, evaluate, execute, the program API (compile, run, results), and
 * every free function.
 */
#include "nift/c_abi.h"

#include <stdio.h>
#include <string.h>

static int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static int is(nift_string s, const char* want) {
    return s.length == strlen(want) && (s.length == 0 || memcmp(s.data, want, s.length) == 0);
}
static nift_source text_source(const char* text) {
    nift_source s;
    s.kind = NIFT_SOURCE_TEXT; s.data = text; s.length = strlen(text);
    s.logical_name = NULL; s.logical_name_length = 0;
    return s;
}

int main(void) {
    nift_string out;
    CHECK(strcmp(nift_abi_version(), NIFT_ABI_VERSION) == 0);
    CHECK(nift_abi_version_major() == NIFT_C_ABI_VERSION_MAJOR);
    CHECK(nift_abi_version_minor() == NIFT_C_ABI_VERSION_MINOR);

    /* Engine, bindings, context, render with a template. */
    nift_engine* engine = nift_engine_new();
    CHECK(engine != NULL);
    CHECK(nift_engine_is_open(engine) == 0);
    CHECK(nift_engine_set_string(engine, "site", 4, "hello", 5) == NIFT_OK);
    CHECK(nift_engine_set_int(engine, "n", 1, 41) == NIFT_OK);
    CHECK(nift_engine_set_json(engine, "j", 1, "{\"k\":[1,2]}", 11) == NIFT_OK);
    nift_context* context = nift_context_new();
    CHECK(context != NULL);
    CHECK(nift_context_set_page_name(context, "index", 5) == NIFT_OK);
    CHECK(nift_context_set_string(context, "who", 3, "world", 5) == NIFT_OK);
    {
        nift_source page = text_source("site=$[site] who=$[who]");
        nift_source tpl = text_source("<main>@content</main>");
        nift_render_result* result = NULL;
        CHECK(nift_engine_render(engine, &page, &tpl, context, &result) == NIFT_OK);
        CHECK(result != NULL && nift_render_result_ok(result) == 1);
        CHECK(nift_render_result_output(result, &out) == NIFT_OK && is(out, "<main>site=hello who=world</main>"));
        nift_render_result_free(result);
    }
    {
        nift_source partial = text_source("[$[site]]");
        nift_render_result* result = NULL;
        CHECK(nift_engine_render_partial(engine, &partial, context, &result) == NIFT_OK);
        CHECK(result != NULL && nift_render_result_ok(result) == 1);
        CHECK(nift_render_result_output(result, &out) == NIFT_OK && is(out, "[hello]"));
        nift_render_result_free(result);
    }
    {
        nift_render_result* result = NULL;
        CHECK(nift_engine_render_text(engine, context, "x$[site]x", 9, &result) == NIFT_OK);
        CHECK(result != NULL && nift_render_result_ok(result) == 1);
        CHECK(nift_render_result_output(result, &out) == NIFT_OK && is(out, "xhellox"));
        nift_render_result_free(result);
    }
    /* Script evaluation and execution. */
    {
        nift_script_result* r = NULL;
        CHECK(nift_engine_evaluate(engine, "n + 1", 5, &r) == NIFT_OK);
        CHECK(r != NULL && nift_script_result_ok(r) == 1);
        CHECK(nift_script_result_value_json(r, &out) == NIFT_OK && is(out, "42"));
        nift_script_result_free(r);
    }
    {
        nift_script_result* r = NULL;
        const char* script = "print(\"ran\");";
        CHECK(nift_engine_execute(engine, script, strlen(script), "cmd", 3, NULL, NULL, 0, &r) == NIFT_OK);
        CHECK(r != NULL && nift_script_result_ok(r) == 1);
        {
            nift_bytes so;
            CHECK(nift_script_result_stdout(r, &so) == NIFT_OK && so.length == 4 && memcmp(so.data, "ran\n", 4) == 0);
        }
        nift_script_result_free(r);
    }
    /* The program API (Runtime 2.0 VM). */
    {
        nift_source s = text_source("x := 0; x += 1; print(x); return x + 41;");
        nift_program* p = NULL;
        nift_program_result* r = NULL;
        CHECK(nift_program_compile(&s, &p, &r) == NIFT_OK && p != NULL);
        CHECK(r != NULL && nift_program_result_outcome(r) == NIFT_PROGRAM_COMPLETE);
        nift_program_result_free(r);
        r = NULL;
        CHECK(nift_program_run(p, 0, 0, 0, &r) == NIFT_OK && r != NULL);
        CHECK(nift_program_result_outcome(r) == NIFT_PROGRAM_COMPLETE);
        CHECK(nift_program_result_output(r, &out) == NIFT_OK && is(out, "1\n"));
        CHECK(nift_program_result_returned(r) == 1);
        CHECK(nift_program_result_return_text(r, &out) == NIFT_OK && is(out, "42"));
        nift_program_result_free(r);
        CHECK(nift_program_free(p) == NIFT_OK);
    }
    {
        nift_source s = text_source("print(");
        nift_program* p = NULL;
        nift_program_result* r = NULL;
        CHECK(nift_program_compile(&s, &p, &r) == NIFT_OK);
        CHECK(p == NULL && r != NULL && nift_program_result_outcome(r) == NIFT_PROGRAM_SYNTAX_ERROR);
        CHECK(nift_program_result_error_message(r, &out) == NIFT_OK && out.length > 0);
        nift_program_result_free(r);
    }
    nift_context_free(context);
    nift_engine_free(engine);
    printf("external consumer: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
