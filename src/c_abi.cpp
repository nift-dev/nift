// CP10: Nift Embed C ABI implementation.
//
// A thin translation boundary over the frozen public C++ Embed API. Every
// exported function contains C++ exceptions (try/catch -> NIFT_ERROR_INTERNAL)
// so nothing crosses the ABI. All handles are opaque; all returned strings are
// borrowed views owned by their owning object. See docs/handover/
// CP10-C-ABI-DESIGN.md for the ownership/lifetime model.
#include "nift/c_abi.h"

#include "nift/context.h"
#include "nift/engine.h"
#include "nift/render_result.h"
#include "nift/source.h"
#include "nift/value.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

// The opaque C handles from the header are completed here (global scope, so
// the definitions match the header's forward declarations). All storage is
// owned by the handle; no C++ type crosses the ABI.
struct nift_engine {
    nift::Engine engine;
    // Scratch storage for borrowed diagnostics. Guarded so writers do not tear
    // the string, but readers must not race writers (caller responsibility).
    mutable std::mutex diag_mutex;
    mutable std::string diag;
    nift_loader_callback loader_cb = nullptr;
    void* loader_user = nullptr;
    nift_environment_callback env_cb = nullptr;
    void* env_user = nullptr;
    // A host callback returned an unexpected status; consumed by the next
    // render call on this engine (approximate attribution under concurrent
    // failing callbacks is documented caller misuse).
    std::atomic<int> pending_callback_status{0};
};

struct nift_context {
    nift::Context context;
};

struct nift_render_result {
    nift::RenderResult result;
};

namespace {

bool valid_input(const char* data, size_t length) {
    return data != nullptr || length == 0;
}

bool valid_binding(const char* name, size_t name_len) {
    return valid_input(name, name_len);
}

nift_status set_out(nift_string* out, const std::string& value) {
    if (out != nullptr) {
        out->data = value.data();
        out->length = value.size();
    }
    return NIFT_OK;
}

void set_engine_diag(const nift_engine* engine, std::string text) {
    std::lock_guard<std::mutex> lock(engine->diag_mutex);
    engine->diag = std::move(text);
}

nift::Source to_source(const nift_source* src) {
    if (src->kind == NIFT_SOURCE_PATH) {
        return nift::Source::path(std::string(src->data, src->length));
    }
    if (src->logical_name != nullptr && src->logical_name_length > 0) {
        return nift::Source::text(std::string(src->data, src->length),
                                  std::string(src->logical_name, src->logical_name_length));
    }
    return nift::Source::text(std::string(src->data, src->length));
}

// Install a C loader callback onto the engine. Only NIFT_OK (with a value,
// possibly empty) and NIFT_ERROR_NOT_FOUND are defined loader outcomes; any
// other status is recorded and surfaces as NIFT_ERROR_CALLBACK on the next
// render (the C++ loader has no error channel, so the render continues as a
// controlled "source unreadable" failure).
void install_loader(nift_engine* engine) {
    nift_loader_callback cb = engine->loader_cb;
    void* user = engine->loader_user;
    if (cb == nullptr) return;
    engine->engine.set_loader([engine, cb, user](std::string_view path) -> std::optional<std::string> {
        nift_string out{nullptr, 0};
        const nift_status status = cb(user, path.data(), path.size(), &out);
        if (status == NIFT_OK) {
            if (out.data == nullptr || out.length == 0) return std::nullopt;
            return std::string(out.data, out.length);
        }
        if (status != NIFT_ERROR_NOT_FOUND) {
            engine->pending_callback_status.store(static_cast<int>(status),
                                                  std::memory_order_relaxed);
        }
        return std::nullopt;
    });
}

void install_environment(nift_engine* engine) {
    nift_environment_callback cb = engine->env_cb;
    void* user = engine->env_user;
    if (cb == nullptr) return;
    engine->engine.set_environment_provider([engine, cb, user](std::string_view name) -> std::optional<std::string> {
        nift_string out{nullptr, 0};
        const nift_status status = cb(user, name.data(), name.size(), &out);
        if (status == NIFT_OK) {
            if (out.data == nullptr || out.length == 0) return std::nullopt;
            return std::string(out.data, out.length);
        }
        if (status != NIFT_ERROR_NOT_FOUND) {
            engine->pending_callback_status.store(static_cast<int>(status),
                                                  std::memory_order_relaxed);
        }
        return std::nullopt;
    });
}

bool consume_callback_error(nift_engine* engine) {
    return engine->pending_callback_status.exchange(0, std::memory_order_relaxed) != 0;
}

}  // namespace

extern "C" {

const char* nift_abi_version(void) { return NIFT_ABI_VERSION; }
unsigned int nift_abi_version_major(void) { return 1; }
unsigned int nift_abi_version_minor(void) { return 0; }

nift_engine* nift_engine_new(void) {
    return new (std::nothrow) nift_engine{};
}

nift_engine* nift_engine_open(const char* root, size_t root_len) {
    if (!valid_input(root, root_len)) return nullptr;
    nift_engine* engine = new (std::nothrow) nift_engine{};
    if (engine == nullptr) return nullptr;
    try {
        engine->engine = nift::Engine(std::string(root, root_len));
    } catch (...) {
        delete engine;
        return nullptr;
    }
    return engine;
}

void nift_engine_free(nift_engine* engine) { delete engine; }

int nift_engine_is_open(const nift_engine* engine) {
    return (engine != nullptr && engine->engine.is_open()) ? 1 : 0;
}

nift_status nift_engine_open_error(const nift_engine* engine, nift_string* out) {
    if (engine == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        set_engine_diag(engine, engine->engine.open_error());
        return set_out(out, engine->diag);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_engine_set_root(nift_engine* engine, const char* root,
                                 size_t root_len) {
    if (engine == nullptr || !valid_input(root, root_len))
        return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        engine->engine.set_root(std::string(root, root_len));
        return NIFT_OK;
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_engine_reload(nift_engine* engine, nift_string* error_out) {
    if (engine == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        std::string error;
        if (!engine->engine.reload(&error)) {
            set_engine_diag(engine, std::move(error));
            if (error_out != nullptr) {
                std::lock_guard<std::mutex> lock(engine->diag_mutex);
                error_out->data = engine->diag.data();
                error_out->length = engine->diag.size();
            }
            return NIFT_ERROR_PROJECT;
        }
        return NIFT_OK;
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

static nift_status set_engine_binding(nift_engine* engine, const char* name,
                                      size_t name_len,
                                      const nift::Value& value) {
    if (!valid_binding(name, name_len)) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        return engine->engine.set(std::string(name, name_len), value) ? NIFT_OK
                                                                      : NIFT_ERROR_INVALID_ARGUMENT;
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_engine_set_string(nift_engine* engine, const char* name,
                                   size_t name_len, const char* value,
                                   size_t value_len) {
    if (engine == nullptr || !valid_input(value, value_len))
        return NIFT_ERROR_INVALID_ARGUMENT;
    return set_engine_binding(engine, name, name_len,
                              nift::Value(std::string(value, value_len)));
}

nift_status nift_engine_set_int(nift_engine* engine, const char* name,
                                size_t name_len, long long value) {
    if (engine == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    return set_engine_binding(engine, name, name_len, nift::Value(static_cast<int>(value)));
}

nift_status nift_engine_set_bool(nift_engine* engine, const char* name,
                                 size_t name_len, int value) {
    if (engine == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    return set_engine_binding(engine, name, name_len, nift::Value(value != 0));
}

nift_status nift_engine_set_json(nift_engine* engine, const char* name,
                                 size_t name_len, const char* json,
                                 size_t json_len) {
    if (engine == nullptr || !valid_input(json, json_len))
        return NIFT_ERROR_INVALID_ARGUMENT;
    if (!valid_binding(name, name_len)) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        return engine->engine.set_json(std::string(name, name_len),
                                       std::string_view(json, json_len))
                   ? NIFT_OK
                   : NIFT_ERROR_INVALID_ARGUMENT;
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_engine_set_loader(nift_engine* engine,
                                   nift_loader_callback callback,
                                   void* user_data) {
    if (engine == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    engine->loader_cb = callback;
    engine->loader_user = user_data;
    try {
        install_loader(engine);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
    return NIFT_OK;
}

nift_status nift_engine_set_environment_provider(nift_engine* engine,
                                                 nift_environment_callback callback,
                                                 void* user_data) {
    if (engine == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    engine->env_cb = callback;
    engine->env_user = user_data;
    try {
        install_environment(engine);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
    return NIFT_OK;
}

nift_context* nift_context_new(void) { return new (std::nothrow) nift_context{}; }

void nift_context_free(nift_context* context) { delete context; }

static nift_status set_context_binding(nift_context* context, const char* name,
                                       size_t name_len,
                                       const nift::Value& value) {
    if (!valid_binding(name, name_len)) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        return context->context.set(std::string(name, name_len), value)
                   ? NIFT_OK
                   : NIFT_ERROR_INVALID_ARGUMENT;
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_context_set_page_name(nift_context* context, const char* name,
                                       size_t name_len) {
    if (context == nullptr || !valid_input(name, name_len))
        return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        context->context.set_page_name(std::string(name, name_len));
        return NIFT_OK;
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_context_set_current_output(nift_context* context,
                                            const char* path, size_t path_len) {
    if (context == nullptr || !valid_input(path, path_len))
        return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        context->context.set_current_output(std::string(path, path_len));
        return NIFT_OK;
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_context_set_title(nift_context* context, const char* title,
                                   size_t title_len) {
    if (context == nullptr || !valid_input(title, title_len))
        return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        context->context.set_title(std::string(title, title_len));
        return NIFT_OK;
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_context_set_string(nift_context* context, const char* name,
                                    size_t name_len, const char* value,
                                    size_t value_len) {
    if (context == nullptr || !valid_input(value, value_len))
        return NIFT_ERROR_INVALID_ARGUMENT;
    return set_context_binding(context, name, name_len,
                               nift::Value(std::string(value, value_len)));
}

nift_status nift_context_set_int(nift_context* context, const char* name,
                                 size_t name_len, long long value) {
    if (context == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    return set_context_binding(context, name, name_len,
                               nift::Value(static_cast<int>(value)));
}

nift_status nift_context_set_bool(nift_context* context, const char* name,
                                  size_t name_len, int value) {
    if (context == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    return set_context_binding(context, name, name_len, nift::Value(value != 0));
}

nift_status nift_context_set_json(nift_context* context, const char* name,
                                  size_t name_len, const char* json,
                                  size_t json_len) {
    if (context == nullptr || !valid_input(json, json_len))
        return NIFT_ERROR_INVALID_ARGUMENT;
    if (!valid_binding(name, name_len)) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        return context->context.set_json(std::string(name, name_len),
                                         std::string_view(json, json_len))
                   ? NIFT_OK
                   : NIFT_ERROR_INVALID_ARGUMENT;
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

static nift_status wrap_render(nift_engine* engine, nift::RenderResult result,
                               nift_render_result** out_result) {
    if (consume_callback_error(engine)) {
        if (out_result != nullptr) *out_result = nullptr;
        return NIFT_ERROR_CALLBACK;
    }
    *out_result = new (std::nothrow) nift_render_result{std::move(result)};
    return *out_result == nullptr ? NIFT_ERROR_INTERNAL : NIFT_OK;
}

nift_status nift_engine_render_page(nift_engine* engine,
                                    const nift_context* context,
                                    const char* page_name, size_t page_name_len,
                                    nift_render_result** out_result) {
    if (engine == nullptr || out_result == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    if (!valid_input(page_name, page_name_len)) return NIFT_ERROR_INVALID_ARGUMENT;
    *out_result = nullptr;
    try {
        if (context != nullptr) {
            return wrap_render(engine,
                               engine->engine.render(std::string_view(page_name, page_name_len),
                                                     context->context),
                               out_result);
        }
        return wrap_render(engine,
                           engine->engine.render(std::string_view(page_name, page_name_len)),
                           out_result);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

static nift_status render_sources(nift_engine* engine, const nift_source* page,
                                  const nift_source* page_template,
                                  const nift_context* context,
                                  nift_render_result** out_result) {
    if (page == nullptr || page_template == nullptr)
        return NIFT_ERROR_INVALID_ARGUMENT;
    if (!valid_input(page->data, page->length) ||
        !valid_input(page_template->data, page_template->length))
        return NIFT_ERROR_INVALID_ARGUMENT;
    if (page->logical_name != nullptr && page->logical_name_length == 0)
        return NIFT_ERROR_INVALID_ARGUMENT;
    *out_result = nullptr;
    try {
        nift::Source page_source = to_source(page);
        nift::Source template_source = to_source(page_template);
        if (context != nullptr) {
            return wrap_render(engine,
                               engine->engine.render(page_source, template_source,
                                                     context->context),
                               out_result);
        }
        return wrap_render(engine,
                           engine->engine.render(page_source, template_source),
                           out_result);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_engine_render(nift_engine* engine, const nift_source* page,
                               const nift_source* page_template,
                               const nift_context* context,
                               nift_render_result** out_result) {
    if (engine == nullptr || out_result == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    return render_sources(engine, page, page_template, context, out_result);
}

nift_status nift_engine_render_partial(nift_engine* engine,
                                       const nift_source* partial,
                                       const nift_context* context,
                                       nift_render_result** out_result) {
    if (engine == nullptr || out_result == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    if (partial == nullptr || !valid_input(partial->data, partial->length))
        return NIFT_ERROR_INVALID_ARGUMENT;
    if (partial->logical_name != nullptr && partial->logical_name_length == 0)
        return NIFT_ERROR_INVALID_ARGUMENT;
    *out_result = nullptr;
    try {
        nift::Source partial_source = to_source(partial);
        if (context != nullptr) {
            return wrap_render(engine,
                               engine->engine.render(partial_source, context->context),
                               out_result);
        }
        return wrap_render(engine, engine->engine.render(partial_source), out_result);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

void nift_render_result_free(nift_render_result* result) { delete result; }

int nift_render_result_ok(const nift_render_result* result) {
    return (result != nullptr && result->result.ok()) ? 1 : 0;
}

nift_status nift_render_result_output(const nift_render_result* result,
                                      nift_string* out) {
    if (result == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        return set_out(out, result->result.output());
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_render_result_error_message(const nift_render_result* result,
                                             nift_string* out) {
    if (result == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        return set_out(out, result->result.error().message);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

nift_status nift_render_result_error_source(const nift_render_result* result,
                                            nift_string* out) {
    if (result == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        return set_out(out, result->result.error().source);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

unsigned long long nift_render_result_error_line(const nift_render_result* result) {
    return result == nullptr ? 0 : result->result.error().line;
}

unsigned long long nift_render_result_error_column(const nift_render_result* result) {
    return result == nullptr ? 0 : result->result.error().column;
}

size_t nift_render_result_pagination_count(const nift_render_result* result) {
    return result == nullptr ? 0 : result->result.pagination().size();
}

nift_status nift_render_result_pagination_get(const nift_render_result* result,
                                              size_t index,
                                              unsigned int* page_out,
                                              nift_string* output_out) {
    if (result == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        const auto& pagination = result->result.pagination();
        if (index >= pagination.size()) return NIFT_ERROR_INVALID_ARGUMENT;
        if (page_out != nullptr) *page_out = static_cast<unsigned int>(pagination[index].page);
        return set_out(output_out, pagination[index].output);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

size_t nift_render_result_dependency_count(const nift_render_result* result) {
    return result == nullptr ? 0 : result->result.dependencies().size();
}

nift_status nift_render_result_dependency_get(const nift_render_result* result,
                                              size_t index, nift_string* out) {
    if (result == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        const auto& dependencies = result->result.dependencies();
        if (index >= dependencies.size()) return NIFT_ERROR_INVALID_ARGUMENT;
        return set_out(out, dependencies[index]);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

size_t nift_render_result_requirement_count(const nift_render_result* result) {
    return result == nullptr ? 0 : result->result.requirements().size();
}

nift_status nift_render_result_requirement_get(const nift_render_result* result,
                                               size_t index, nift_string* out) {
    if (result == nullptr) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        const auto& requirements = result->result.requirements();
        if (index >= requirements.size()) return NIFT_ERROR_INVALID_ARGUMENT;
        return set_out(out, requirements[index]);
    } catch (...) {
        return NIFT_ERROR_INTERNAL;
    }
}

}  // extern "C"
