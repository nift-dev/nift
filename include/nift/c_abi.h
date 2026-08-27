/* Nift Embed C ABI — public header.
 *
 * A small, stable, ownership-explicit C interface representing the frozen Nift
 * Embed contract: engine, context, render, complete pagination, dependencies,
 * requirements, diagnostics, and the loader/environment host seams. It is the
 * long-lived foundation for production bindings (Go/Node/Python/C#/...).
 *
 * Deliberately NOT exposed: build, .unfinished, tracked persistence,
 * .info.json, repair, hashes, watch, filesystem output mutation.
 *
 * This header compiles as C. See docs/handover/CP10-C-ABI-DESIGN.md for the
 * ownership/lifetime model, status/error model, callback model and thread
 * safety.
 */
#ifndef NIFT_C_ABI_H
#define NIFT_C_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Versioning                                                               */
/* ------------------------------------------------------------------------ */

#define NIFT_ABI_VERSION "1.0"

/* Returns NIFT_ABI_VERSION ("1.0"). Additive ABI extensions keep the version;
 * incompatible changes bump the minor/major. Symbol names are nift_*. */
const char* nift_abi_version(void);
unsigned int nift_abi_version_major(void);
unsigned int nift_abi_version_minor(void);

/* ------------------------------------------------------------------------ */
/* Status / error model                                                     */
/* ------------------------------------------------------------------------ */

/* Mechanical status of an ABI call. Render *semantics* are reported through
 * the returned nift_render_result (ok + diagnostic), exactly like the frozen
 * C++ contract; these categories describe the translation layer. */
typedef enum {
    NIFT_OK = 0,
    NIFT_ERROR_INVALID_ARGUMENT, /* null handle, bad length, invalid binding name, malformed JSON argument */
    NIFT_ERROR_PROJECT,          /* project open/reload failed; diagnostic in the nift_string out */
    NIFT_ERROR_NOT_FOUND,        /* callback supplied no value (loader miss / environment unset) */
    NIFT_ERROR_CALLBACK,         /* loader/environment callback returned an error status */
    NIFT_ERROR_INTERNAL          /* an unexpected C++ exception was contained */
} nift_status;

/* ------------------------------------------------------------------------ */
/* Types                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct nift_engine nift_engine;
typedef struct nift_context nift_context;
typedef struct nift_render_result nift_render_result;

/* A borrowed UTF-8 string view. Valid only for the documented lifetime of the
 * object it was obtained from; copy to retain. `data` may be NULL when
 * `length` is 0 (the empty string). Inputs: data==NULL && length==0 is empty;
 * data==NULL && length>0 is NIFT_ERROR_INVALID_ARGUMENT. Embedded NULs are
 * carried by length; NUL termination is never assumed or promised. */
typedef struct {
    const char* data;
    size_t length;
} nift_string;

typedef enum {
    NIFT_SOURCE_TEXT = 0,
    NIFT_SOURCE_PATH = 1
} nift_source_kind;

/* Caller-owned input for render/partial. A path source is resolved against the
 * engine root. `logical_name`/`logical_name_length` are optional (NULL/0 =
 * none) and give an in-memory text source an identity for diagnostics. */
typedef struct {
    nift_source_kind kind;
    const char* data;
    size_t length;
    const char* logical_name;
    size_t logical_name_length;
} nift_source;

/* Host seams (called by render threads; must be thread-safe). Callback output
 * semantics per status:
 *   NIFT_OK                out = the returned value (length 0 = valid empty)
 *   NIFT_ERROR_NOT_FOUND   out ignored (absent/unset)
 *   hard failure (other)   out = the failure diagnostic (borrowed, copied by
 *                          the engine); empty out = generic "host callback
 *                          failed"
 * A host failure travels through the render computation itself and is
 * reported as a failed RenderResult with the diagnostic, identically on the
 * caller thread and on the pagination worker threads - the render calls still
 * return NIFT_OK (the operation completed mechanically) and the RESULT carries
 * ok=false. No side channel or TLS attribution is involved. */
/* Loader: return NIFT_OK and fill *out (borrowed, copied by the engine) when
 * the source exists; NIFT_ERROR_NOT_FOUND when it does not; any other status
 * is a host failure reported on the failed RenderResult, with *out used as the
 * diagnostic when non-empty. */
typedef nift_status (*nift_loader_callback)(void* user_data, const char* path,
                                            size_t path_len, nift_string* out);
/* Environment provider: NIFT_OK + value when set, NIFT_ERROR_NOT_FOUND when
 * unset, other status -> NIFT_ERROR_CALLBACK. */
typedef nift_status (*nift_environment_callback)(void* user_data,
                                                 const char* name,
                                                 size_t name_len,
                                                 nift_string* out);

/* ------------------------------------------------------------------------ */
/* Engine                                                                   */
/* ------------------------------------------------------------------------ */

/* Standalone engine (deterministic; never walks the filesystem). */
nift_engine* nift_engine_new(void);
/* Project-aware engine: opens the Nift project at `root` (UTF-8, length).
 * Non-throwing: check nift_engine_is_open / nift_engine_open_error. */
nift_engine* nift_engine_open(const char* root, size_t root_len);
void nift_engine_free(nift_engine* engine);

int nift_engine_is_open(const nift_engine* engine);
/* Borrowed diagnostic (valid until the next diagnostic call on this engine or
 * nift_engine_free). out may be NULL. */
nift_status nift_engine_open_error(const nift_engine* engine, nift_string* out);

/* Base directory for resolving relative path sources and relative @input.
 * Mutating the engine is not thread-safe with active renders. */
nift_status nift_engine_set_root(nift_engine* engine, const char* root,
                                 size_t root_len);

/* Atomic snapshot replacement (see C++ Engine::reload). Safe concurrently with
 * renders. error_out is borrowed scratch (may be NULL). */
nift_status nift_engine_reload(nift_engine* engine, nift_string* error_out);

/* Long-lived default bindings (resolved before @json/contracts/metadata).
 * Binding names must be valid Nift identifiers and not structural built-ins
 * (name, content-path, output-path, template-path, loop). */
nift_status nift_engine_set_string(nift_engine* engine, const char* name,
                                   size_t name_len, const char* value,
                                   size_t value_len);
nift_status nift_engine_set_int(nift_engine* engine, const char* name,
                                size_t name_len, int32_t value);
nift_status nift_engine_set_number(nift_engine* engine, const char* name,
                                   size_t name_len, double value);
nift_status nift_engine_set_bool(nift_engine* engine, const char* name,
                                 size_t name_len, int value);
/* JSON text (UTF-8, length). Malformed JSON -> NIFT_ERROR_INVALID_ARGUMENT. */
nift_status nift_engine_set_json(nift_engine* engine, const char* name,
                                 size_t name_len, const char* json,
                                 size_t json_len);

/* Host seams. Installing a loader/env provider is engine mutation (not
 * thread-safe with active renders). */
nift_status nift_engine_set_loader(nift_engine* engine,
                                   nift_loader_callback callback,
                                   void* user_data);
nift_status nift_engine_set_environment_provider(nift_engine* engine,
                                                 nift_environment_callback callback,
                                                 void* user_data);

/* ------------------------------------------------------------------------ */
/* Context (per-render request state)                                       */
/* ------------------------------------------------------------------------ */

nift_context* nift_context_new(void);
void nift_context_free(nift_context* context);

nift_status nift_context_set_page_name(nift_context* context, const char* name,
                                       size_t name_len);
/* The generated output location of the current page, used by @pathto. */
nift_status nift_context_set_current_output(nift_context* context,
                                            const char* path, size_t path_len);
nift_status nift_context_set_title(nift_context* context, const char* title,
                                   size_t title_len);
nift_status nift_context_set_string(nift_context* context, const char* name,
                                    size_t name_len, const char* value,
                                    size_t value_len);
nift_status nift_context_set_int(nift_context* context, const char* name,
                                 size_t name_len, int32_t value);
nift_status nift_context_set_number(nift_context* context, const char* name,
                                    size_t name_len, double value);
nift_status nift_context_set_bool(nift_context* context, const char* name,
                                  size_t name_len, int value);
nift_status nift_context_set_json(nift_context* context, const char* name,
                                  size_t name_len, const char* json,
                                  size_t json_len);

/* ------------------------------------------------------------------------ */
/* Render                                                                   */
/* ------------------------------------------------------------------------ */

/* Project-aware tracked-page render: page content/template/output geometry from
 * the project snapshot; complete pagination (output = page 1; pagination =
 * pages 2..N ascending with explicit 1-based page numbers). The page name is
 * authoritative over any Context page name. ctx may be NULL. The outcome (ok /
 * diagnostic) is in the returned result; the call itself returns NIFT_OK when
 * mechanically valid. */
nift_status nift_engine_render_page(nift_engine* engine,
                                    const nift_context* context,
                                    const char* page_name, size_t page_name_len,
                                    nift_render_result** out_result);

/* Full page + template composition (template must contain exactly one
 * @content). page and page_template are caller-owned inputs. ctx may be NULL. */
nift_status nift_engine_render(nift_engine* engine,
                               const nift_source* page,
                               const nift_source* page_template,
                               const nift_context* context,
                               nift_render_result** out_result);

/* Standalone partial/fragment render (a partial containing @content is an
 * error). */
nift_status nift_engine_render_partial(nift_engine* engine,
                                       const nift_source* partial,
                                       const nift_context* context,
                                       nift_render_result** out_result);

/* Standalone filesystem-source render (nift_engine_render_path): the path is
 * ALWAYS a filesystem path - a missing path is a controlled missing-path error
 * and is never reinterpreted as template text. Standalone in-memory-source
 * render (nift_engine_render_text): the text is ALWAYS template source and is
 * never checked against the filesystem. Both are partial renders (no @content
 * slot). ctx may be NULL (a fresh empty context). These distinct entry points
 * let production bindings render a path or text source without ever inferring
 * the source kind from filesystem state. */
nift_status nift_engine_render_path(nift_engine* engine,
                                    const nift_context* context,
                                    const char* path, size_t path_len,
                                    nift_render_result** out_result);
nift_status nift_engine_render_text(nift_engine* engine,
                                    const nift_context* context,
                                    const char* text, size_t text_len,
                                    nift_render_result** out_result);

/* ------------------------------------------------------------------------ */
/* Result                                                                   */
/* ------------------------------------------------------------------------ */

void nift_render_result_free(nift_render_result* result);

int nift_render_result_ok(const nift_render_result* result);
/* Borrowed views valid until the result is freed. out may be NULL (query
 * availability only where sensible). */
nift_status nift_render_result_output(const nift_render_result* result,
                                      nift_string* out);
nift_status nift_render_result_error_message(const nift_render_result* result,
                                             nift_string* out);
nift_status nift_render_result_error_source(const nift_render_result* result,
                                            nift_string* out);
unsigned long long nift_render_result_error_line(const nift_render_result* result);
unsigned long long nift_render_result_error_column(const nift_render_result* result);

/* Pagination: pages 2..N ascending, 1-based page numbers. */
size_t nift_render_result_pagination_count(const nift_render_result* result);
nift_status nift_render_result_pagination_get(const nift_render_result* result,
                                              size_t index,
                                              unsigned int* page_out,
                                              nift_string* output_out);

/* Dependencies / requirements (root-relative spellings; borrowed views). */
size_t nift_render_result_dependency_count(const nift_render_result* result);
nift_status nift_render_result_dependency_get(const nift_render_result* result,
                                              size_t index, nift_string* out);
size_t nift_render_result_requirement_count(const nift_render_result* result);
nift_status nift_render_result_requirement_get(const nift_render_result* result,
                                               size_t index, nift_string* out);

#ifdef __cplusplus
}
#endif

#endif /* NIFT_C_ABI_H */
