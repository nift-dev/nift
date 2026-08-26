// Nift Embed Node binding - N-API native addon over the frozen Nift C ABI.
//
// Threading model (deliberate, see docs/handover/CP14-NODE-DESIGN.md):
//
//   JS render() call
//        |                       (napi_async_work)
//        v
//   libuv worker thread runs the C ABI render (synchronous C++)
//        |
//        +---> host callback (loader/env) fires on a render thread
//        |        (the render thread or a C++ pagination worker)
//        |        |
//        |        v
//        |     napi_call_threadsafe_function (blocking) queues the callback
//        |     onto the JS event loop; the render thread waits on a
//        |     condition variable
//        |        |
//        |        v
//        |     call_js_cb runs the user's JS loader/env on the JS thread,
//        |     stores the result (value / NotFound / Error diagnostic) and
//        |     signals the waiting render thread
//        |
//        +---> result built; complete_cb resolves the JS Promise
//
// This avoids the JS-thread deadlock a synchronous render would cause: the JS
// thread stays free to service the threadsafe-function callbacks while the
// render runs on a worker thread. The C ABI's synchronous-from-C++ host
// callback is bridged by the blocking threadsafe-function call plus a
// condition variable. The user's loader/env callback therefore runs on the JS
// thread and MUST return synchronously (a string, null/undefined, or throw).
//
// Callback `out` buffer lifetime: the C ABI copies the borrowed `out`
// synchronously immediately after the callback returns (c_abi.cpp
// callback_result). Each native render thread owns a per-thread scratch
// CallbackScratch reused only after the previous same-thread use is provably
// complete (same-thread sequentiality + synchronous copy) and freed at thread
// exit. This is NOT the cross-thread free-on-next-callback pattern CP11 ruled
// out.
//
// The zero-`unsafe` gate is scoped to the Rust crates; this FFI addon uses
// N-API and the C ABI directly.
#include <node_api.h>

#include "nift/c_abi.h"

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {

constexpr int kModePage = 0;
constexpr int kModeComposed = 1;
constexpr int kModePartial = 2;

// ---------------------------------------------------------------------------
// N-API helpers
// ---------------------------------------------------------------------------

[[noreturn]] void Fail(napi_env env, napi_status status, const char* what) {
  char msg[256];
  std::snprintf(msg, sizeof(msg), "Nift native: %s (napi_status %d)", what,
                static_cast<int>(status));
  fprintf(stderr, "NIFT_NATIVE_FAIL: %s\n", msg);
  fflush(stderr);
  napi_throw_error(env, nullptr, msg);
  std::abort();  // unreachable after napi_throw_error
}

#define NAPI_CHECK(env, expr)                                    \
  do {                                                           \
    napi_status s_ = (expr);                                     \
    if (s_ != napi_ok) Fail(env, s_, #expr);                     \
  } while (0)

napi_value ThrowJs(napi_env env, const std::string& message) {
  napi_throw_error(env, nullptr, message.c_str());
  return nullptr;
}

std::string GetString(napi_env env, napi_value value) {
  size_t len = 0;
  NAPI_CHECK(env, napi_get_value_string_utf8(env, value, nullptr, 0, &len));
  std::string out(len, '\0');
  NAPI_CHECK(env, napi_get_value_string_utf8(env, value, out.data(), len + 1, &len));
  out.resize(len);
  return out;
}

napi_value NewString(napi_env env, const std::string& s) {
  napi_value out;
  NAPI_CHECK(env, napi_create_string_utf8(env, s.data(), s.size(), &out));
  return out;
}

// A render source argument is either a string (in-memory text) or an object
// { path } / { text }.
bool SourceArg(napi_env env, napi_value value, std::string* data, bool* is_path) {
  if (value == nullptr) return false;
  napi_valuetype t;
  if (napi_typeof(env, value, &t) != napi_ok) return false;
  if (t == napi_string) {
    *data = GetString(env, value);
    *is_path = false;
    return true;
  }
  if (t == napi_object) {
    bool has = false;
    if (napi_has_named_property(env, value, "path", &has) == napi_ok && has) {
      napi_value p = nullptr;
      if (napi_get_named_property(env, value, "path", &p) != napi_ok) return false;
      napi_valuetype pt;
      if (napi_typeof(env, p, &pt) != napi_ok || pt != napi_string) return false;
      *data = GetString(env, p);
      *is_path = true;
      return true;
    }
    if (napi_has_named_property(env, value, "text", &has) == napi_ok && has) {
      napi_value p = nullptr;
      if (napi_get_named_property(env, value, "text", &p) != napi_ok) return false;
      napi_valuetype pt;
      if (napi_typeof(env, p, &pt) != napi_ok || pt != napi_string) return false;
      *data = GetString(env, p);
      *is_path = false;
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// CallbackScratch: per-native-thread scratch for one host callback exchange
// ---------------------------------------------------------------------------

struct CallbackScratch {
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  nift_status status = NIFT_ERROR_INTERNAL;
  std::string path;   // written by the render thread, read by call_js_cb
  std::string value;  // result (value on Found, diagnostic on Error)
};

thread_local CallbackScratch* tls_scratch = nullptr;

struct ScratchGuard {
  ~ScratchGuard() {
    delete tls_scratch;
    tls_scratch = nullptr;
  }
};
thread_local ScratchGuard tls_guard;

CallbackScratch* GetScratch() {
  if (tls_scratch == nullptr) {
    tls_scratch = new CallbackScratch();
  }
  return tls_scratch;
}

void SetDone(CallbackScratch* s, nift_status status, const std::string& value) {
  std::lock_guard<std::mutex> lk(s->m);
  s->status = status;
  s->value = value;
  s->done = true;
  s->cv.notify_all();
}

// ---------------------------------------------------------------------------
// Engine / Context wrappers
// ---------------------------------------------------------------------------

struct EngineWrap {
  napi_env env = nullptr;
  nift_engine* engine = nullptr;
  napi_ref loader_ref = nullptr;
  napi_ref env_ref = nullptr;
  napi_threadsafe_function loader_tsfn = nullptr;
  napi_threadsafe_function env_tsfn = nullptr;
  bool disposed = false;
};

struct ContextWrap {
  nift_context* ctx = nullptr;
  bool disposed = false;
};

// ---------------------------------------------------------------------------
// Host callback dispatch (render/pagination worker thread side)
// ---------------------------------------------------------------------------

nift_status DispatchHostCallback(EngineWrap* e, napi_threadsafe_function tsfn,
                                 CallbackScratch* s, const std::string& arg) {
  {
    std::lock_guard<std::mutex> lk(s->m);
    s->path = arg;
    s->done = false;
    s->status = NIFT_ERROR_INTERNAL;
    s->value.clear();
  }
  napi_status ns = napi_call_threadsafe_function(tsfn, s, napi_tsfn_blocking);
  if (ns != napi_ok) {
    // Closing/aborted tsfn: surface as a controlled host failure without
    // waiting on the condition variable (the JS callback will not run).
    return NIFT_ERROR_CALLBACK;
  }
  std::unique_lock<std::mutex> lk(s->m);
  s->cv.wait(lk, [&] { return s->done; });
  return s->status;
}

nift_status LoaderNative(void* user_data, const char* path, size_t path_len,
                         nift_string* out) {
  EngineWrap* e = static_cast<EngineWrap*>(user_data);
  if (e == nullptr || e->loader_tsfn == nullptr) {
    return NIFT_ERROR_NOT_FOUND;
  }
  CallbackScratch* s = GetScratch();
  nift_status status =
      DispatchHostCallback(e, e->loader_tsfn, s, std::string(path, path_len));
  if (status == NIFT_OK || status == NIFT_ERROR_CALLBACK) {
    out->data = s->value.data();
    out->length = s->value.size();
  }
  return status;
}

nift_status EnvironmentNative(void* user_data, const char* name, size_t name_len,
                              nift_string* out) {
  EngineWrap* e = static_cast<EngineWrap*>(user_data);
  if (e == nullptr || e->env_tsfn == nullptr) {
    return NIFT_ERROR_NOT_FOUND;
  }
  CallbackScratch* s = GetScratch();
  nift_status status =
      DispatchHostCallback(e, e->env_tsfn, s, std::string(name, name_len));
  if (status == NIFT_OK || status == NIFT_ERROR_CALLBACK) {
    out->data = s->value.data();
    out->length = s->value.size();
  }
  return status;
}

// ---------------------------------------------------------------------------
// JS-thread side of the threadsafe function (one per host seam)
// ---------------------------------------------------------------------------

// Calls `fn` (a user callback) with one string argument and turns the outcome
// into the callback scratch. Returns via SetDone.
void RunUserCallback(napi_env env, CallbackScratch* s, napi_ref user_ref,
                     const char* must_return_msg) {
  if (user_ref == nullptr) {
    SetDone(s, NIFT_ERROR_NOT_FOUND, "");
    return;
  }
  napi_value global = nullptr, fn = nullptr, arg_js = nullptr, result = nullptr;
  NAPI_CHECK(env, napi_get_global(env, &global));
  NAPI_CHECK(env, napi_get_reference_value(env, user_ref, &fn));
  NAPI_CHECK(env, napi_create_string_utf8(env, s->path.data(), s->path.size(), &arg_js));
  napi_value argv[1] = {arg_js};
  napi_status call = napi_call_function(env, global, fn, 1, argv, &result);
  if (call != napi_ok) {
    bool pending = false;
    napi_is_exception_pending(env, &pending);
    if (pending) {
      napi_value ex = nullptr;
      napi_get_and_clear_last_exception(env, &ex);
      // A thrown Error contributes its message (exact diagnostic contract);
      // anything else is coerced to a string.
      napi_value msg = nullptr;
      bool has_msg = false;
      if (napi_has_named_property(env, ex, "message", &has_msg) == napi_ok &&
          has_msg && napi_get_named_property(env, ex, "message", &msg) == napi_ok) {
        napi_valuetype mt;
        if (napi_typeof(env, msg, &mt) == napi_ok && mt == napi_string) {
          SetDone(s, NIFT_ERROR_CALLBACK, GetString(env, msg));
          return;
        }
      }
      napi_value ex_str = nullptr;
      napi_coerce_to_string(env, ex, &ex_str);
      SetDone(s, NIFT_ERROR_CALLBACK, GetString(env, ex_str));
    } else {
      SetDone(s, NIFT_ERROR_CALLBACK, must_return_msg);
    }
    return;
  }
  napi_valuetype type;
  NAPI_CHECK(env, napi_typeof(env, result, &type));
  if (type == napi_null || type == napi_undefined) {
    SetDone(s, NIFT_ERROR_NOT_FOUND, "");
  } else if (type == napi_string) {
    SetDone(s, NIFT_OK, GetString(env, result));
  } else {
    SetDone(s, NIFT_ERROR_CALLBACK, must_return_msg);
  }
}

void CompleteLoaderCallback(napi_env env, napi_value js_cb, void* context,
                            void* data) {
  EngineWrap* e = static_cast<EngineWrap*>(context);
  CallbackScratch* s = static_cast<CallbackScratch*>(data);
  (void)js_cb;
  if (e == nullptr) {
    SetDone(s, NIFT_ERROR_CALLBACK, "engine disposed during loader callback");
    return;
  }
  RunUserCallback(env, s, e->loader_ref,
                  "loader must return a string, null/undefined, or throw");
}

void CompleteEnvironmentCallback(napi_env env, napi_value js_cb, void* context,
                                 void* data) {
  EngineWrap* e = static_cast<EngineWrap*>(context);
  CallbackScratch* s = static_cast<CallbackScratch*>(data);
  (void)js_cb;
  if (e == nullptr) {
    SetDone(s, NIFT_ERROR_CALLBACK, "engine disposed during environment callback");
    return;
  }
  RunUserCallback(env, s, e->env_ref,
                  "environment provider must return a string, null/undefined, or throw");
}

// ---------------------------------------------------------------------------
// Async render request
// ---------------------------------------------------------------------------

struct RenderReq {
  napi_env env = nullptr;
  nift_engine* engine = nullptr;
  nift_context* ctx = nullptr;
  int mode = kModeComposed;
  std::string page_name;
  std::string page;
  std::string tpl;
  bool page_is_path = false;
  bool tpl_is_path = false;
  nift_status rc = NIFT_OK;
  nift_render_result* result = nullptr;
  napi_deferred deferred = nullptr;
  napi_ref engine_ref = nullptr;  // roots the engine during the async render
  napi_ref ctx_ref = nullptr;     // roots the context during the async render
  napi_async_work work = nullptr;
};

void BuildSource(const std::string& data, bool is_path, nift_source* out) {
  out->kind = is_path ? NIFT_SOURCE_PATH : NIFT_SOURCE_TEXT;
  out->data = data.data();
  out->length = data.size();
  out->logical_name = nullptr;
  out->logical_name_length = 0;
}

void RenderExecute(napi_env env, void* data) {
  RenderReq* req = static_cast<RenderReq*>(data);
  switch (req->mode) {
    case kModePage:
      req->rc = nift_engine_render_page(req->engine, req->ctx,
                                        req->page_name.data(), req->page_name.size(),
                                        &req->result);
      break;
    case kModePartial: {
      nift_source partial;
      BuildSource(req->page, req->page_is_path, &partial);
      req->rc = nift_engine_render_partial(req->engine, &partial, req->ctx,
                                           &req->result);
      break;
    }
    default: {
      nift_source page, tpl;
      BuildSource(req->page, req->page_is_path, &page);
      BuildSource(req->tpl, req->tpl_is_path, &tpl);
      req->rc = nift_engine_render(req->engine, &page, &tpl, req->ctx, &req->result);
      break;
    }
  }
  if (req->rc != NIFT_OK && req->result != nullptr) {
    nift_render_result_free(req->result);
    req->result = nullptr;
  }
}

napi_value ReadString(napi_env env, nift_render_result* result,
                      nift_status (*fn)(const nift_render_result*, nift_string*)) {
  nift_string s{nullptr, 0};
  if (fn(result, &s) != NIFT_OK || s.data == nullptr) {
    return NewString(env, "");
  }
  return NewString(env, std::string(s.data, s.length));
}

void RenderComplete(napi_env env, napi_status status, void* data) {
  RenderReq* req = static_cast<RenderReq*>(data);
  napi_value result_value = nullptr;

  if (status != napi_ok || req->rc != NIFT_OK) {
    napi_value err = nullptr;
    NAPI_CHECK(env, napi_create_error(env, nullptr,
                                      NewString(env, "render call failed"), &err));
    NAPI_CHECK(env, napi_reject_deferred(env, req->deferred, err));
  } else {
    int ok = nift_render_result_ok(req->result);
    NAPI_CHECK(env, napi_create_object(env, &result_value));
    napi_value ok_value = nullptr;
    NAPI_CHECK(env, napi_get_boolean(env, ok != 0, &ok_value));
    NAPI_CHECK(env, napi_set_named_property(env, result_value, "ok", ok_value));
    if (ok != 0) {
      NAPI_CHECK(env, napi_set_named_property(
                          env, result_value, "output",
                          ReadString(env, req->result, nift_render_result_output)));
      napi_value pagination = nullptr, deps = nullptr, reqs = nullptr;
      NAPI_CHECK(env, napi_create_array(env, &pagination));
      NAPI_CHECK(env, napi_create_array(env, &deps));
      NAPI_CHECK(env, napi_create_array(env, &reqs));

      size_t page_count = nift_render_result_pagination_count(req->result);
      for (size_t i = 0; i < page_count; ++i) {
        unsigned int page_no = 0;
        nift_string page_out{nullptr, 0};
        if (nift_render_result_pagination_get(req->result, i, &page_no, &page_out) == NIFT_OK) {
          napi_value item = nullptr, no = nullptr, out = nullptr;
          NAPI_CHECK(env, napi_create_object(env, &item));
          NAPI_CHECK(env, napi_create_uint32(env, page_no, &no));
          NAPI_CHECK(env, napi_create_string_utf8(env, page_out.data, page_out.length, &out));
          NAPI_CHECK(env, napi_set_named_property(env, item, "page", no));
          NAPI_CHECK(env, napi_set_named_property(env, item, "output", out));
          NAPI_CHECK(env, napi_set_element(env, pagination, i, item));
        }
      }
      size_t dep_count = nift_render_result_dependency_count(req->result);
      for (size_t i = 0; i < dep_count; ++i) {
        nift_string d{nullptr, 0};
        if (nift_render_result_dependency_get(req->result, i, &d) == NIFT_OK) {
          NAPI_CHECK(env, napi_set_element(env, deps, i,
                                           NewString(env, std::string(d.data, d.length))));
        }
      }
      size_t req_count = nift_render_result_requirement_count(req->result);
      for (size_t i = 0; i < req_count; ++i) {
        nift_string r{nullptr, 0};
        if (nift_render_result_requirement_get(req->result, i, &r) == NIFT_OK) {
          NAPI_CHECK(env, napi_set_element(env, reqs, i,
                                           NewString(env, std::string(r.data, r.length))));
        }
      }
      NAPI_CHECK(env, napi_set_named_property(env, result_value, "pagination", pagination));
      NAPI_CHECK(env, napi_set_named_property(env, result_value, "dependencies", deps));
      NAPI_CHECK(env, napi_set_named_property(env, result_value, "requirements", reqs));
    } else {
      NAPI_CHECK(env, napi_set_named_property(
                          env, result_value, "error",
                          ReadString(env, req->result, nift_render_result_error_message)));
      NAPI_CHECK(env, napi_set_named_property(
                          env, result_value, "errorSource",
                          ReadString(env, req->result, nift_render_result_error_source)));
      napi_value line = nullptr, col = nullptr;
      NAPI_CHECK(env, napi_create_uint32(
                          env, static_cast<uint32_t>(nift_render_result_error_line(req->result)),
                          &line));
      NAPI_CHECK(env, napi_create_uint32(
                          env, static_cast<uint32_t>(nift_render_result_error_column(req->result)),
                          &col));
      NAPI_CHECK(env, napi_set_named_property(env, result_value, "errorLine", line));
      NAPI_CHECK(env, napi_set_named_property(env, result_value, "errorColumn", col));
    }
    NAPI_CHECK(env, napi_resolve_deferred(env, req->deferred, result_value));
  }

  if (req->result != nullptr) nift_render_result_free(req->result);
  if (req->engine_ref != nullptr) NAPI_CHECK(env, napi_delete_reference(env, req->engine_ref));
  if (req->ctx_ref != nullptr) NAPI_CHECK(env, napi_delete_reference(env, req->ctx_ref));
  NAPI_CHECK(env, napi_delete_async_work(env, req->work));
  delete req;
}

// ---------------------------------------------------------------------------
// Engine methods
// ---------------------------------------------------------------------------

ContextWrap* UnwrapContext(napi_env env, napi_value value, const char* what);

EngineWrap* UnwrapEngine(napi_env env, napi_value value) {
  EngineWrap* e = nullptr;
  if (napi_unwrap(env, value, reinterpret_cast<void**>(&e)) != napi_ok ||
      e == nullptr) {
    ThrowJs(env, "Nift native: expected an Engine");
    return nullptr;
  }
  if (e->disposed) {
    ThrowJs(env, "Nift native: Engine has been disposed");
    return nullptr;
  }
  return e;
}

void FinalizeEngine(napi_env env, void* data, void* hint) {
  EngineWrap* e = static_cast<EngineWrap*>(data);
  if (e->loader_ref != nullptr) napi_delete_reference(e->env, e->loader_ref);
  if (e->env_ref != nullptr) napi_delete_reference(e->env, e->env_ref);
  if (e->loader_tsfn != nullptr) {
    napi_release_threadsafe_function(e->loader_tsfn, napi_tsfn_release);
  }
  if (e->env_tsfn != nullptr) {
    napi_release_threadsafe_function(e->env_tsfn, napi_tsfn_release);
  }
  if (e->engine != nullptr) nift_engine_free(e->engine);
  delete e;
}

EngineWrap* CreateEngineWrap(napi_env env, nift_engine* engine) {
  EngineWrap* e = new EngineWrap();
  e->env = env;
  e->engine = engine;
  napi_value resource_name = nullptr;
  NAPI_CHECK(env, napi_create_string_utf8(env, "nift.hostcallbacks",
                                          NAPI_AUTO_LENGTH, &resource_name));
  NAPI_CHECK(env, napi_create_threadsafe_function(
                      env, nullptr, nullptr, resource_name, 0, 1, nullptr,
                      nullptr, e, CompleteLoaderCallback, &e->loader_tsfn));
  NAPI_CHECK(env, napi_create_threadsafe_function(
                      env, nullptr, nullptr, resource_name, 0, 1, nullptr,
                      nullptr, e, CompleteEnvironmentCallback, &e->env_tsfn));
  return e;
}

napi_value EngineNew(napi_env env, napi_callback_info info) {
  (void)info;
  napi_value obj = nullptr;
  NAPI_CHECK(env, napi_create_object(env, &obj));
  nift_engine* engine = nift_engine_new();
  if (engine == nullptr) return ThrowJs(env, "nift_engine_new returned null");
  EngineWrap* e = CreateEngineWrap(env, engine);
  NAPI_CHECK(env, napi_wrap(env, obj, e, FinalizeEngine, nullptr, nullptr));
  return obj;
}

napi_value EngineOpen(napi_env env, napi_callback_info info) {
  napi_value args[1] = {nullptr};
  size_t argc = 1;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, nullptr, nullptr));
  if (argc < 1) return ThrowJs(env, "open(root) requires a root path");
  std::string root = GetString(env, args[0]);
  nift_engine* engine = nift_engine_open(root.data(), root.size());
  if (engine == nullptr) return ThrowJs(env, "nift_engine_open returned null");
  napi_value obj = nullptr;
  NAPI_CHECK(env, napi_create_object(env, &obj));
  EngineWrap* e = CreateEngineWrap(env, engine);
  NAPI_CHECK(env, napi_wrap(env, obj, e, FinalizeEngine, nullptr, nullptr));
  return obj;
}

napi_value EngineDispose(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr;
  NAPI_CHECK(env, napi_get_cb_info(env, info, nullptr, nullptr, &this_value, nullptr));
  EngineWrap* e = nullptr;
  if (napi_unwrap(env, this_value, reinterpret_cast<void**>(&e)) == napi_ok &&
      e != nullptr) {
    // Remove the wrap so the GC finalizer cannot run again, then free eagerly.
    NAPI_CHECK(env, napi_remove_wrap(env, this_value, reinterpret_cast<void**>(&e)));
    FinalizeEngine(env, e, nullptr);
  }
  return this_value;
}

napi_value EngineIsOpen(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr;
  NAPI_CHECK(env, napi_get_cb_info(env, info, nullptr, nullptr, &this_value, nullptr));
  EngineWrap* e = UnwrapEngine(env, this_value);
  if (e == nullptr) return nullptr;
  napi_value out = nullptr;
  NAPI_CHECK(env, napi_get_boolean(env, nift_engine_is_open(e->engine) != 0, &out));
  return out;
}

napi_value EngineSetRoot(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr, args[1] = {nullptr};
  size_t argc = 1;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, &this_value, nullptr));
  EngineWrap* e = UnwrapEngine(env, this_value);
  if (e == nullptr) return nullptr;
  if (argc < 1) return ThrowJs(env, "setRoot(root) requires a path");
  std::string root = GetString(env, args[0]);
  if (nift_engine_set_root(e->engine, root.data(), root.size()) != NIFT_OK) {
    return ThrowJs(env, "nift_engine_set_root failed");
  }
  return this_value;
}

napi_value EngineReload(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr;
  NAPI_CHECK(env, napi_get_cb_info(env, info, nullptr, nullptr, &this_value, nullptr));
  EngineWrap* e = UnwrapEngine(env, this_value);
  if (e == nullptr) return nullptr;
  nift_string err{nullptr, 0};
  nift_status rc = nift_engine_reload(e->engine, &err);
  if (rc != NIFT_OK) {
    return ThrowJs(env, err.length > 0 ? std::string(err.data, err.length)
                                       : "nift_engine_reload failed");
  }
  return this_value;
}

napi_value SetStringValue(napi_env env, napi_callback_info info, bool on_engine) {
  napi_value this_value = nullptr, args[2] = {nullptr, nullptr};
  size_t argc = 2;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, &this_value, nullptr));
  if (argc < 2) return ThrowJs(env, "setString(name, value) requires two arguments");
  std::string name = GetString(env, args[0]);
  std::string value = GetString(env, args[1]);
  nift_status rc;
  if (on_engine) {
    EngineWrap* e = UnwrapEngine(env, this_value);
    if (e == nullptr) return nullptr;
    rc = nift_engine_set_string(e->engine, name.data(), name.size(),
                                value.data(), value.size());
  } else {
    ContextWrap* c = UnwrapContext(env, this_value, "Context");
    if (c == nullptr) return nullptr;
    rc = nift_context_set_string(c->ctx, name.data(), name.size(),
                                 value.data(), value.size());
  }
  if (rc != NIFT_OK) return ThrowJs(env, "invalid binding name: " + name);
  return this_value;
}

napi_value EngineSetString(napi_env env, napi_callback_info info) {
  return SetStringValue(env, info, true);
}

napi_value EngineSetJSON(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr, args[2] = {nullptr, nullptr};
  size_t argc = 2;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, &this_value, nullptr));
  if (argc < 2) return ThrowJs(env, "setJSON(name, json) requires two arguments");
  std::string name = GetString(env, args[0]);
  std::string json = GetString(env, args[1]);
  EngineWrap* e = UnwrapEngine(env, this_value);
  if (e == nullptr) return nullptr;
  if (nift_engine_set_json(e->engine, name.data(), name.size(), json.data(),
                           json.size()) != NIFT_OK) {
    return ThrowJs(env, "invalid binding name: " + name);
  }
  return this_value;
}

napi_value SetScalarValue(napi_env env, napi_callback_info info, bool on_engine,
                          int is_int, int is_bool) {
  napi_value this_value = nullptr, args[2] = {nullptr, nullptr};
  size_t argc = 2;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, &this_value, nullptr));
  if (argc < 2) return ThrowJs(env, "setter requires (name, value)");
  std::string name = GetString(env, args[0]);
  nift_status rc = NIFT_OK;
  if (is_bool) {
    bool b = false;
    NAPI_CHECK(env, napi_get_value_bool(env, args[1], &b));
    if (on_engine) {
      EngineWrap* e = UnwrapEngine(env, this_value);
      if (e == nullptr) return nullptr;
      rc = nift_engine_set_bool(e->engine, name.data(), name.size(), b ? 1 : 0);
    } else {
      ContextWrap* c = UnwrapContext(env, this_value, "Context");
      if (c == nullptr) return nullptr;
      rc = nift_context_set_bool(c->ctx, name.data(), name.size(), b ? 1 : 0);
    }
  } else if (is_int) {
    int32_t v = 0;
    NAPI_CHECK(env, napi_get_value_int32(env, args[1], &v));
    if (on_engine) {
      EngineWrap* e = UnwrapEngine(env, this_value);
      if (e == nullptr) return nullptr;
      rc = nift_engine_set_int(e->engine, name.data(), name.size(), v);
    } else {
      ContextWrap* c = UnwrapContext(env, this_value, "Context");
      if (c == nullptr) return nullptr;
      rc = nift_context_set_int(c->ctx, name.data(), name.size(), v);
    }
  } else {
    double v = 0;
    NAPI_CHECK(env, napi_get_value_double(env, args[1], &v));
    if (on_engine) {
      EngineWrap* e = UnwrapEngine(env, this_value);
      if (e == nullptr) return nullptr;
      rc = nift_engine_set_number(e->engine, name.data(), name.size(), v);
    } else {
      ContextWrap* c = UnwrapContext(env, this_value, "Context");
      if (c == nullptr) return nullptr;
      rc = nift_context_set_number(c->ctx, name.data(), name.size(), v);
    }
  }
  if (rc != NIFT_OK) return ThrowJs(env, "invalid binding name: " + name);
  return this_value;
}

napi_value EngineSetInt(napi_env env, napi_callback_info info) {
  return SetScalarValue(env, info, true, 1, 0);
}
napi_value EngineSetNumber(napi_env env, napi_callback_info info) {
  return SetScalarValue(env, info, true, 0, 0);
}
napi_value EngineSetBool(napi_env env, napi_callback_info info) {
  return SetScalarValue(env, info, true, 0, 1);
}

napi_value EngineSetLoader(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr, args[1] = {nullptr};
  size_t argc = 1;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, &this_value, nullptr));
  EngineWrap* e = UnwrapEngine(env, this_value);
  if (e == nullptr) return nullptr;
  if (e->loader_ref != nullptr) {
    NAPI_CHECK(env, napi_delete_reference(env, e->loader_ref));
    e->loader_ref = nullptr;
  }
  if (argc >= 1) {
    napi_valuetype t;
    NAPI_CHECK(env, napi_typeof(env, args[0], &t));
    if (t == napi_function) {
      NAPI_CHECK(env, napi_create_reference(env, args[0], 1, &e->loader_ref));
    }
  }
  nift_status set_rc = nift_engine_set_loader(e->engine, LoaderNative, e);
  if (set_rc != NIFT_OK) {
    return ThrowJs(env, "nift_engine_set_loader failed");
  }
  return this_value;
}

napi_value EngineSetEnvironmentProvider(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr, args[1] = {nullptr};
  size_t argc = 1;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, &this_value, nullptr));
  EngineWrap* e = UnwrapEngine(env, this_value);
  if (e == nullptr) return nullptr;
  if (e->env_ref != nullptr) {
    NAPI_CHECK(env, napi_delete_reference(env, e->env_ref));
    e->env_ref = nullptr;
  }
  if (argc >= 1) {
    napi_valuetype t;
    NAPI_CHECK(env, napi_typeof(env, args[0], &t));
    if (t == napi_function) {
      NAPI_CHECK(env, napi_create_reference(env, args[0], 1, &e->env_ref));
    }
  }
  if (nift_engine_set_environment_provider(e->engine, EnvironmentNative, e) != NIFT_OK) {
    return ThrowJs(env, "nift_engine_set_environment_provider failed");
  }
  return this_value;
}

// ---------------------------------------------------------------------------
// Async render
// ---------------------------------------------------------------------------

// Args depend on mode:
//   page:    [pageName, ctx?]
//   composed:[page, template, ctx?]
//   partial: [partial, ctx?]
napi_value BeginRender(napi_env env, napi_callback_info info, int mode) {
  napi_value this_value = nullptr, args[4] = {nullptr};
  size_t argc = 4;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, &this_value, nullptr));
  EngineWrap* e = UnwrapEngine(env, this_value);
  if (e == nullptr) return nullptr;

  RenderReq* req = new RenderReq();
  req->env = env;
  req->engine = e->engine;
  req->mode = mode;

  napi_value ctx_value = nullptr;
  if (mode == kModePage) {
    if (argc >= 1) req->page_name = GetString(env, args[0]);
    if (argc >= 2) ctx_value = args[1];
  } else if (mode == kModeComposed) {
    if (argc >= 1 && !SourceArg(env, args[0], &req->page, &req->page_is_path)) {
      delete req;
      return ThrowJs(env, "render(page, template, ctx) expects strings or {path}/{text} objects");
    }
    if (argc >= 2 && !SourceArg(env, args[1], &req->tpl, &req->tpl_is_path)) {
      delete req;
      return ThrowJs(env, "render(page, template, ctx) expects strings or {path}/{text} objects");
    }
    if (argc >= 3) ctx_value = args[2];
  } else {
    if (argc >= 1 && !SourceArg(env, args[0], &req->page, &req->page_is_path)) {
      delete req;
      return ThrowJs(env, "renderPartial(partial, ctx) expects a string or {path}/{text} object");
    }
    if (argc >= 2) ctx_value = args[1];
  }

  if (ctx_value != nullptr) {
    napi_valuetype t;
    NAPI_CHECK(env, napi_typeof(env, ctx_value, &t));
    if (t != napi_null && t != napi_undefined) {
      ContextWrap* c = UnwrapContext(env, ctx_value, "Context");
      if (c == nullptr) {
        delete req;
        return nullptr;
      }
      req->ctx = c->ctx;
      NAPI_CHECK(env, napi_create_reference(env, ctx_value, 1, &req->ctx_ref));
    }
  }

  napi_value promise = nullptr;
  NAPI_CHECK(env, napi_create_promise(env, &req->deferred, &promise));
  NAPI_CHECK(env, napi_create_reference(env, this_value, 1, &req->engine_ref));

  napi_value resource_name = nullptr;
  NAPI_CHECK(env, napi_create_string_utf8(env, "nift.render", NAPI_AUTO_LENGTH,
                                          &resource_name));
  napi_value resource = nullptr;
  NAPI_CHECK(env, napi_create_object(env, &resource));
  NAPI_CHECK(env, napi_create_async_work(env, resource, resource_name,
                                         RenderExecute, RenderComplete, req,
                                         &req->work));
  NAPI_CHECK(env, napi_queue_async_work(env, req->work));
  return promise;
}

napi_value RenderPage(napi_env env, napi_callback_info info) {
  return BeginRender(env, info, kModePage);
}
napi_value Render(napi_env env, napi_callback_info info) {
  return BeginRender(env, info, kModeComposed);
}
napi_value RenderPartial(napi_env env, napi_callback_info info) {
  return BeginRender(env, info, kModePartial);
}

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------

ContextWrap* UnwrapContext(napi_env env, napi_value value, const char* what) {
  ContextWrap* c = nullptr;
  if (napi_unwrap(env, value, reinterpret_cast<void**>(&c)) != napi_ok ||
      c == nullptr) {
    ThrowJs(env, std::string("Nift native: expected ") + what);
    return nullptr;
  }
  if (c->disposed) {
    ThrowJs(env, "Nift native: Context has been disposed");
    return nullptr;
  }
  return c;
}

void FinalizeContext(napi_env env, void* data, void* hint) {
  ContextWrap* c = static_cast<ContextWrap*>(data);
  if (c->ctx != nullptr) nift_context_free(c->ctx);
  delete c;
}

napi_value ContextNew(napi_env env, napi_callback_info info) {
  (void)info;
  nift_context* ctx = nift_context_new();
  if (ctx == nullptr) return ThrowJs(env, "nift_context_new returned null");
  napi_value obj = nullptr;
  NAPI_CHECK(env, napi_create_object(env, &obj));
  ContextWrap* c = new ContextWrap();
  c->ctx = ctx;
  NAPI_CHECK(env, napi_wrap(env, obj, c, FinalizeContext, nullptr, nullptr));
  return obj;
}

napi_value ContextDispose(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr;
  NAPI_CHECK(env, napi_get_cb_info(env, info, nullptr, nullptr, &this_value, nullptr));
  ContextWrap* c = nullptr;
  if (napi_unwrap(env, this_value, reinterpret_cast<void**>(&c)) == napi_ok &&
      c != nullptr) {
    NAPI_CHECK(env, napi_remove_wrap(env, this_value, reinterpret_cast<void**>(&c)));
    FinalizeContext(env, c, nullptr);
  }
  return this_value;
}

napi_value ContextSetPageName(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr, args[1] = {nullptr};
  size_t argc = 1;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, &this_value, nullptr));
  ContextWrap* c = UnwrapContext(env, this_value, "Context");
  if (c == nullptr) return nullptr;
  std::string name = argc >= 1 ? GetString(env, args[0]) : "";
  if (nift_context_set_page_name(c->ctx, name.data(), name.size()) != NIFT_OK) {
    return ThrowJs(env, "invalid context page name");
  }
  return this_value;
}

napi_value ContextSetCurrentOutput(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr, args[1] = {nullptr};
  size_t argc = 1;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, &this_value, nullptr));
  ContextWrap* c = UnwrapContext(env, this_value, "Context");
  if (c == nullptr) return nullptr;
  std::string path = argc >= 1 ? GetString(env, args[0]) : "";
  if (nift_context_set_current_output(c->ctx, path.data(), path.size()) != NIFT_OK) {
    return ThrowJs(env, "invalid context current output");
  }
  return this_value;
}

napi_value ContextSetString(napi_env env, napi_callback_info info) {
  return SetStringValue(env, info, false);
}
napi_value ContextSetJSON(napi_env env, napi_callback_info info) {
  napi_value this_value = nullptr, args[2] = {nullptr, nullptr};
  size_t argc = 2;
  NAPI_CHECK(env, napi_get_cb_info(env, info, &argc, args, &this_value, nullptr));
  if (argc < 2) return ThrowJs(env, "setJSON(name, json) requires two arguments");
  std::string name = GetString(env, args[0]);
  std::string json = GetString(env, args[1]);
  ContextWrap* c = UnwrapContext(env, this_value, "Context");
  if (c == nullptr) return nullptr;
  if (nift_context_set_json(c->ctx, name.data(), name.size(), json.data(),
                            json.size()) != NIFT_OK) {
    return ThrowJs(env, "invalid binding name: " + name);
  }
  return this_value;
}
napi_value ContextSetInt(napi_env env, napi_callback_info info) {
  return SetScalarValue(env, info, false, 1, 0);
}
napi_value ContextSetNumber(napi_env env, napi_callback_info info) {
  return SetScalarValue(env, info, false, 0, 0);
}
napi_value ContextSetBool(napi_env env, napi_callback_info info) {
  return SetScalarValue(env, info, false, 0, 1);
}

// ---------------------------------------------------------------------------
// Module init
// ---------------------------------------------------------------------------

napi_value Init(napi_env env, napi_value exports) {
  const napi_property_descriptor props[] = {
      {"newEngine", nullptr, EngineNew, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"openEngine", nullptr, EngineOpen, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"disposeEngine", nullptr, EngineDispose, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineIsOpen", nullptr, EngineIsOpen, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineSetRoot", nullptr, EngineSetRoot, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineReload", nullptr, EngineReload, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineSetString", nullptr, EngineSetString, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineSetInt", nullptr, EngineSetInt, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineSetNumber", nullptr, EngineSetNumber, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineSetBool", nullptr, EngineSetBool, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineSetJSON", nullptr, EngineSetJSON, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineSetLoader", nullptr, EngineSetLoader, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineSetEnvironmentProvider", nullptr, EngineSetEnvironmentProvider, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineRenderPage", nullptr, RenderPage, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineRender", nullptr, Render, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"engineRenderPartial", nullptr, RenderPartial, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"newContext", nullptr, ContextNew, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"disposeContext", nullptr, ContextDispose, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"contextSetPageName", nullptr, ContextSetPageName, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"contextSetCurrentOutput", nullptr, ContextSetCurrentOutput, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"contextSetString", nullptr, ContextSetString, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"contextSetInt", nullptr, ContextSetInt, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"contextSetNumber", nullptr, ContextSetNumber, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"contextSetBool", nullptr, ContextSetBool, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"contextSetJSON", nullptr, ContextSetJSON, nullptr, nullptr, nullptr, napi_default, nullptr},
  };
  NAPI_CHECK(env, napi_define_properties(env, exports,
                                         sizeof(props) / sizeof(props[0]), props));
  return exports;
}

}  // namespace

#ifndef NODE_GYP_MODULE_NAME
#define NODE_GYP_MODULE_NAME nift_node
#endif
NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
