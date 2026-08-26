// Nift Embed Python binding - CPython C extension over the frozen Nift C ABI.
//
// GIL / threading model (deliberate, see docs/handover/CP15-PYTHON-DESIGN.md):
//
//   python render() call
//        |
//        v
//   Py_BEGIN_ALLOW_THREADS (GIL released)
//        |
//        v
//   C ABI render runs (synchronous C++, on this thread + C++ pagination
//   workers)
//        |
//        +---> loader/env callback fires on a render thread
//        |        (this thread or a C++ pagination worker)
//        |        |
//        |        v
//        |     PyGILState_Ensure() re-acquires the GIL
//        |        |
//        |        v
//        |     Python loader/env runs; result (str/bytes/None/raise) is turned
//        |     into a nift_status + borrowed nift_string; GIL released
//        |
//        v
//   Py_END_ALLOW_THREADS (GIL re-acquired); result object built
//
// A synchronous render with the GIL released during the C++ call avoids the
// deadlock that a GIL-held render would cause: callbacks from pagination
// workers can always acquire the GIL because the calling thread released it.
//
// Callback `out` buffer lifetime: the C ABI copies the borrowed `out`
// synchronously immediately after the callback returns (c_abi.cpp
// callback_result). Each native render thread owns a thread_local std::string
// scratch reused only after the previous same-thread use is provably complete
// and freed at thread exit. This is NOT the cross-thread free-on-next-callback
// pattern CP11 ruled out.
//
// Lifetime: close() marks an object disposed immediately (new operations are
// rejected) and defers native destruction until render_count reaches zero; a
// synchronous render holds a strong reference (Py_INCREF) to the Engine/Context
// for its duration, so the object can never be deallocated mid-render.
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "nift/c_abi.h"

#include <string>

namespace {

// ---------------------------------------------------------------------------
// Engine / Context objects
// ---------------------------------------------------------------------------

struct NiftEngineObject {
  PyObject_HEAD
  nift_engine* engine = nullptr;
  PyObject* loader = nullptr;  // callable or None
  PyObject* env = nullptr;     // callable or None
  Py_ssize_t render_count = 0;
  int disposed = 0;
  int destroyed = 0;
};

struct NiftContextObject {
  PyObject_HEAD
  nift_context* ctx = nullptr;
  Py_ssize_t render_count = 0;
  int disposed = 0;
  int destroyed = 0;
};

extern PyTypeObject NiftEngineType;
extern PyTypeObject NiftContextType;

// ---------------------------------------------------------------------------
// thread_local scratch for callback `out` buffers
// ---------------------------------------------------------------------------

thread_local std::string* tls_callback_scratch = nullptr;

struct ScratchGuard {
  ~ScratchGuard() {
    delete tls_callback_scratch;
    tls_callback_scratch = nullptr;
  }
};
thread_local ScratchGuard tls_scratch_guard;

// ---------------------------------------------------------------------------
// Host callback dispatch (render/pagination worker thread side)
// ---------------------------------------------------------------------------

nift_status RunUserCallback(NiftEngineObject* self, PyObject* fn,
                            const std::string& arg, nift_string* out) {
  PyGILState_STATE gil = PyGILState_Ensure();
  nift_status status = NIFT_ERROR_CALLBACK;
  std::string diagnostic;

  PyObject* arg_utf8 = PyUnicode_DecodeUTF8(arg.data(), (Py_ssize_t)arg.size(),
                                            "strict");
  PyObject* result = nullptr;
  if (arg_utf8 != nullptr) {
    result = PyObject_CallFunctionObjArgs(fn, arg_utf8, nullptr);
    Py_DECREF(arg_utf8);
  }
  if (result == nullptr) {
    PyObject *etype = nullptr, *evalue = nullptr, *etrace = nullptr;
    PyErr_Fetch(&etype, &evalue, &etrace);
    PyErr_NormalizeException(&etype, &evalue, &etrace);
    PyObject* msg = evalue != nullptr ? PyObject_Str(evalue) : nullptr;
    if (msg != nullptr) {
      const char* s = PyUnicode_AsUTF8(msg);
      if (s != nullptr) diagnostic = s;
      Py_DECREF(msg);
    }
    if (diagnostic.empty()) diagnostic = "host callback failed";
    Py_XDECREF(etype);
    Py_XDECREF(evalue);
    Py_XDECREF(etrace);
    status = NIFT_ERROR_CALLBACK;
  } else if (result == Py_None) {
    status = NIFT_ERROR_NOT_FOUND;
  } else if (PyUnicode_Check(result)) {
    Py_ssize_t len = 0;
    const char* s = PyUnicode_AsUTF8AndSize(result, &len);
    if (s != nullptr) {
      std::string* scratch = tls_callback_scratch;
      if (scratch == nullptr) {
        scratch = new std::string();
        tls_callback_scratch = scratch;
      }
      scratch->assign(s, (size_t)len);
      out->data = scratch->data();
      out->length = scratch->size();
      status = NIFT_OK;
    } else {
      diagnostic = "loader/env returned a str that could not be encoded";
    }
  } else if (PyBytes_Check(result)) {
    char* s = nullptr;
    Py_ssize_t len = 0;
    if (PyBytes_AsStringAndSize(result, &s, &len) == 0 && s != nullptr) {
      std::string* scratch = tls_callback_scratch;
      if (scratch == nullptr) {
        scratch = new std::string();
        tls_callback_scratch = scratch;
      }
      scratch->assign(s, (size_t)len);
      out->data = scratch->data();
      out->length = scratch->size();
      status = NIFT_OK;
    } else {
      diagnostic = "loader/env returned invalid bytes";
    }
  } else {
    diagnostic =
        "loader/env must return a str, bytes, None, or raise an exception";
  }
  Py_XDECREF(result);
  if (status == NIFT_ERROR_CALLBACK && out != nullptr && !diagnostic.empty()) {
    std::string* scratch = tls_callback_scratch;
    if (scratch == nullptr) {
      scratch = new std::string();
      tls_callback_scratch = scratch;
    }
    scratch->assign(diagnostic);
    out->data = scratch->data();
    out->length = scratch->size();
  }
  PyGILState_Release(gil);
  return status;
}

nift_status LoaderNative(void* user_data, const char* path, size_t path_len,
                         nift_string* out) {
  NiftEngineObject* self = static_cast<NiftEngineObject*>(user_data);
  if (self == nullptr || self->loader == nullptr || self->loader == Py_None) {
    return NIFT_ERROR_NOT_FOUND;
  }
  return RunUserCallback(self, self->loader, std::string(path, path_len), out);
}

nift_status EnvironmentNative(void* user_data, const char* name, size_t name_len,
                              nift_string* out) {
  NiftEngineObject* self = static_cast<NiftEngineObject*>(user_data);
  if (self == nullptr || self->env == nullptr || self->env == Py_None) {
    return NIFT_ERROR_NOT_FOUND;
  }
  return RunUserCallback(self, self->env, std::string(name, name_len), out);
}

// ---------------------------------------------------------------------------
// Native destruction (deferred)
// ---------------------------------------------------------------------------

void DestroyEngine(NiftEngineObject* self) {
  if (self->destroyed) return;
  self->destroyed = 1;
  if (self->engine != nullptr) nift_engine_free(self->engine);
  self->engine = nullptr;
}

void DestroyContext(NiftContextObject* self) {
  if (self->destroyed) return;
  self->destroyed = 1;
  if (self->ctx != nullptr) nift_context_free(self->ctx);
  self->ctx = nullptr;
}

// ---------------------------------------------------------------------------
// Engine type
// ---------------------------------------------------------------------------

void EngineDealloc(PyObject* obj) {
  NiftEngineObject* self = reinterpret_cast<NiftEngineObject*>(obj);
  // A render holds a strong reference, so render_count is zero here.
  DestroyEngine(self);
  Py_CLEAR(self->loader);
  Py_CLEAR(self->env);
  Py_TYPE(self)->tp_free(obj);
}

PyObject* EngineNew(PyObject*, PyObject*) {
  NiftEngineObject* self =
      PyObject_New(NiftEngineObject, &NiftEngineType);
  if (self == nullptr) return nullptr;
  self->engine = nift_engine_new();
  self->loader = Py_NewRef(Py_None);
  self->env = Py_NewRef(Py_None);
  self->render_count = 0;
  self->disposed = 0;
  self->destroyed = 0;
  if (self->engine == nullptr) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "nift_engine_new returned null");
    return nullptr;
  }
  return reinterpret_cast<PyObject*>(self);
}

PyObject* EngineOpen(PyObject*, PyObject* args) {
  const char* root = nullptr;
  Py_ssize_t root_len = 0;
  if (!PyArg_ParseTuple(args, "s#", &root, &root_len)) return nullptr;
  NiftEngineObject* self = PyObject_New(NiftEngineObject, &NiftEngineType);
  if (self == nullptr) return nullptr;
  self->loader = Py_NewRef(Py_None);
  self->env = Py_NewRef(Py_None);
  self->engine = nift_engine_open(root, (size_t)root_len);
  self->render_count = 0;
  self->disposed = 0;
  self->destroyed = 0;
  if (self->engine == nullptr) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "nift_engine_open returned null");
    return nullptr;
  }
  return reinterpret_cast<PyObject*>(self);
}

NiftEngineObject* EngineFromArgs(PyObject* args) {
  PyObject* obj = nullptr;
  if (!PyArg_ParseTuple(args, "O", &obj)) return nullptr;
  if (!PyObject_TypeCheck(obj, &NiftEngineType)) {
    PyErr_SetString(PyExc_TypeError, "expected an Engine");
    return nullptr;
  }
  NiftEngineObject* self = reinterpret_cast<NiftEngineObject*>(obj);
  if (self->disposed || self->destroyed) {
    PyErr_SetString(PyExc_RuntimeError, "Engine has been disposed");
    return nullptr;
  }
  return self;
}

PyObject* EngineDispose(PyObject*, PyObject* args) {
  PyObject* obj = nullptr;
  if (!PyArg_ParseTuple(args, "O", &obj)) return nullptr;
  if (!PyObject_TypeCheck(obj, &NiftEngineType)) {
    Py_RETURN_NONE;
  }
  NiftEngineObject* self = reinterpret_cast<NiftEngineObject*>(obj);
  self->disposed = 1;
  if (self->render_count == 0) DestroyEngine(self);
  Py_RETURN_NONE;
}

PyObject* EngineIsOpen(PyObject*, PyObject* args) {
  NiftEngineObject* self = EngineFromArgs(args);
  if (self == nullptr) return nullptr;
  if (self->engine == nullptr) Py_RETURN_FALSE;
  if (nift_engine_is_open(self->engine) != 0) Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

PyObject* EngineSetRoot(PyObject*, PyObject* args) {
  PyObject* obj = nullptr;
  const char* root = nullptr;
  Py_ssize_t root_len = 0;
  if (!PyArg_ParseTuple(args, "Os#", &obj, &root, &root_len)) return nullptr;
  if (!PyObject_TypeCheck(obj, &NiftEngineType)) {
    PyErr_SetString(PyExc_TypeError, "expected an Engine");
    return nullptr;
  }
  NiftEngineObject* self = reinterpret_cast<NiftEngineObject*>(obj);
  if (self->disposed || self->destroyed) {
    PyErr_SetString(PyExc_RuntimeError, "Engine has been disposed");
    return nullptr;
  }
  if (nift_engine_set_root(self->engine, root, (size_t)root_len) != NIFT_OK) {
    PyErr_SetString(PyExc_RuntimeError, "nift_engine_set_root failed");
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject* EngineReload(PyObject*, PyObject* args) {
  NiftEngineObject* self = EngineFromArgs(args);
  if (self == nullptr) return nullptr;
  nift_string err{nullptr, 0};
  nift_status rc = nift_engine_reload(self->engine, &err);
  if (rc != NIFT_OK) {
    std::string msg = err.length > 0 ? std::string(err.data, err.length)
                                     : "nift_engine_reload failed";
    PyErr_SetString(PyExc_RuntimeError, msg.c_str());
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject* EngineSetStringValue(PyObject* self_obj, const char* name,
                               Py_ssize_t name_len, const char* value,
                               Py_ssize_t value_len, bool is_json) {
  NiftEngineObject* self = reinterpret_cast<NiftEngineObject*>(self_obj);
  nift_status rc;
  if (is_json) {
    rc = nift_engine_set_json(self->engine, name, (size_t)name_len, value,
                              (size_t)value_len);
  } else {
    rc = nift_engine_set_string(self->engine, name, (size_t)name_len, value,
                                (size_t)value_len);
  }
  if (rc != NIFT_OK) {
    PyErr_Format(PyExc_RuntimeError, "invalid binding name: %s", name);
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject* EngineSetString(PyObject*, PyObject* args) {
  PyObject* obj = nullptr;
  const char* name = nullptr;
  Py_ssize_t name_len = 0;
  const char* value = nullptr;
  Py_ssize_t value_len = 0;
  if (!PyArg_ParseTuple(args, "Os#s#", &obj, &name, &name_len, &value, &value_len))
    return nullptr;
  if (!PyObject_TypeCheck(obj, &NiftEngineType)) {
    PyErr_SetString(PyExc_TypeError, "expected an Engine");
    return nullptr;
  }
  NiftEngineObject* self = reinterpret_cast<NiftEngineObject*>(obj);
  if (self->disposed || self->destroyed) {
    PyErr_SetString(PyExc_RuntimeError, "Engine has been disposed");
    return nullptr;
  }
  return EngineSetStringValue(obj, name, name_len, value, value_len, false);
}

PyObject* EngineSetJSON(PyObject*, PyObject* args) {
  PyObject* obj = nullptr;
  const char* name = nullptr;
  Py_ssize_t name_len = 0;
  const char* json = nullptr;
  Py_ssize_t json_len = 0;
  if (!PyArg_ParseTuple(args, "Os#s#", &obj, &name, &name_len, &json, &json_len))
    return nullptr;
  if (!PyObject_TypeCheck(obj, &NiftEngineType)) {
    PyErr_SetString(PyExc_TypeError, "expected an Engine");
    return nullptr;
  }
  NiftEngineObject* self = reinterpret_cast<NiftEngineObject*>(obj);
  if (self->disposed || self->destroyed) {
    PyErr_SetString(PyExc_RuntimeError, "Engine has been disposed");
    return nullptr;
  }
  return EngineSetStringValue(obj, name, name_len, json, json_len, true);
}

template <typename Fn>
PyObject* SetScalar(PyObject* args, Fn fn) {
  PyObject* obj = nullptr;
  const char* name = nullptr;
  Py_ssize_t name_len = 0;
  PyObject* value = nullptr;
  if (!PyArg_ParseTuple(args, "Os#O", &obj, &name, &name_len, &value))
    return nullptr;
  if (!PyObject_TypeCheck(obj, &NiftEngineType)) {
    PyErr_SetString(PyExc_TypeError, "expected an Engine");
    return nullptr;
  }
  NiftEngineObject* self = reinterpret_cast<NiftEngineObject*>(obj);
  if (self->disposed || self->destroyed) {
    PyErr_SetString(PyExc_RuntimeError, "Engine has been disposed");
    return nullptr;
  }
  nift_status rc = fn(self->engine, name, (size_t)name_len, value);
  if (rc != NIFT_OK) {
    PyErr_Format(PyExc_RuntimeError, "invalid binding name: %s", name);
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject* EngineSetInt(PyObject*, PyObject* args) {
  return SetScalar(args, [](nift_engine* e, const char* n, size_t nl,
                            PyObject* v) {
    long iv = PyLong_AsLong(v);
    if (iv == -1 && PyErr_Occurred()) return NIFT_ERROR_INVALID_ARGUMENT;
    return nift_engine_set_int(e, n, nl, (int32_t)iv);
  });
}

PyObject* EngineSetNumber(PyObject*, PyObject* args) {
  return SetScalar(args, [](nift_engine* e, const char* n, size_t nl,
                            PyObject* v) {
    double d = PyFloat_AsDouble(v);
    if (d == -1.0 && PyErr_Occurred()) return NIFT_ERROR_INVALID_ARGUMENT;
    return nift_engine_set_number(e, n, nl, d);
  });
}

PyObject* EngineSetBool(PyObject*, PyObject* args) {
  return SetScalar(args, [](nift_engine* e, const char* n, size_t nl,
                            PyObject* v) {
    int b = PyObject_IsTrue(v);
    if (b < 0) return NIFT_ERROR_INVALID_ARGUMENT;
    return nift_engine_set_bool(e, n, nl, b);
  });
}

PyObject* SetCallback(PyObject* args, bool loader) {
  PyObject* obj = nullptr;
  PyObject* fn = Py_None;
  if (!PyArg_ParseTuple(args, "O|O", &obj, &fn)) return nullptr;
  if (!PyObject_TypeCheck(obj, &NiftEngineType)) {
    PyErr_SetString(PyExc_TypeError, "expected an Engine");
    return nullptr;
  }
  NiftEngineObject* self = reinterpret_cast<NiftEngineObject*>(obj);
  if (self->disposed || self->destroyed) {
    PyErr_SetString(PyExc_RuntimeError, "Engine has been disposed");
    return nullptr;
  }
  if (fn != Py_None && !PyCallable_Check(fn)) {
    PyErr_SetString(PyExc_TypeError, "expected a callable or None");
    return nullptr;
  }
  PyObject** slot = loader ? &self->loader : &self->env;
  Py_INCREF(fn);
  Py_SETREF(*slot, fn);
  nift_status rc = loader
                       ? nift_engine_set_loader(self->engine, LoaderNative, self)
                       : nift_engine_set_environment_provider(self->engine,
                                                              EnvironmentNative,
                                                              self);
  if (rc != NIFT_OK) {
    PyErr_SetString(PyExc_RuntimeError, "failed to install host callback");
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject* EngineSetLoader(PyObject*, PyObject* args) {
  return SetCallback(args, true);
}
PyObject* EngineSetEnvironmentProvider(PyObject*, PyObject* args) {
  return SetCallback(args, false);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

PyObject* StrFromNift(nift_string s) {
  return PyUnicode_DecodeUTF8(s.data == nullptr ? "" : s.data, (Py_ssize_t)s.length,
                              "strict");
}

// Parse a render source argument: str/bytes -> text; (kind, bytes) -> source.
bool SourceFromObject(PyObject* src, int* kind, std::string* data) {
  if (PyUnicode_Check(src)) {
    Py_ssize_t len = 0;
    const char* s = PyUnicode_AsUTF8AndSize(src, &len);
    if (s == nullptr) return false;
    *kind = NIFT_SOURCE_TEXT;
    data->assign(s, (size_t)len);
    return true;
  }
  if (PyBytes_Check(src)) {
    char* s = nullptr;
    Py_ssize_t len = 0;
    if (PyBytes_AsStringAndSize(src, &s, &len) != 0 || s == nullptr) return false;
    *kind = NIFT_SOURCE_TEXT;
    data->assign(s, (size_t)len);
    return true;
  }
  if (PyTuple_Check(src) && PyTuple_Size(src) == 2) {
    long k = PyLong_AsLong(PyTuple_GET_ITEM(src, 0));
    if (k != NIFT_SOURCE_TEXT && k != NIFT_SOURCE_PATH) return false;
    PyObject* payload = PyTuple_GET_ITEM(src, 1);
    Py_ssize_t len = 0;
    const char* ptr = nullptr;
    if (PyUnicode_Check(payload)) {
      ptr = PyUnicode_AsUTF8AndSize(payload, &len);
    } else if (PyBytes_Check(payload)) {
      char* bs = nullptr;
      if (PyBytes_AsStringAndSize(payload, &bs, &len) != 0 || bs == nullptr)
        return false;
      ptr = bs;
    } else {
      return false;
    }
    if (ptr == nullptr) return false;
    *kind = (int)k;
    data->assign(ptr, (size_t)len);
    return true;
  }
  return false;
}

PyObject* DoRender(PyObject* args, int mode) {
  // args: (engine, ...source..., ctx_or_none)
  PyObject* engine_obj = nullptr;
  PyObject* ctx_obj = Py_None;
  PyObject* a0 = nullptr;
  PyObject* a1 = nullptr;
  if (mode == 0) {  // page
    if (!PyArg_ParseTuple(args, "O" "O" "|O", &engine_obj, &a0, &ctx_obj))
      return nullptr;
  } else if (mode == 1) {  // composed
    if (!PyArg_ParseTuple(args, "O" "O" "O" "|O", &engine_obj, &a0, &a1, &ctx_obj))
      return nullptr;
  } else {  // partial
    if (!PyArg_ParseTuple(args, "O" "O" "|O", &engine_obj, &a0, &ctx_obj))
      return nullptr;
  }
  if (!PyObject_TypeCheck(engine_obj, &NiftEngineType)) {
    PyErr_SetString(PyExc_TypeError, "expected an Engine");
    return nullptr;
  }
  NiftEngineObject* self = reinterpret_cast<NiftEngineObject*>(engine_obj);
  if (self->disposed || self->destroyed) {
    PyErr_SetString(PyExc_RuntimeError, "Engine has been disposed");
    return nullptr;
  }
  NiftContextObject* ctx = nullptr;
  if (ctx_obj != Py_None) {
    if (!PyObject_TypeCheck(ctx_obj, &NiftContextType)) {
      PyErr_SetString(PyExc_TypeError, "expected a Context or None");
      return nullptr;
    }
    ctx = reinterpret_cast<NiftContextObject*>(ctx_obj);
    if (ctx->disposed || ctx->destroyed) {
      PyErr_SetString(PyExc_RuntimeError, "Context has been disposed");
      return nullptr;
    }
  }

  std::string page_name;
  std::string page_data;
  std::string tpl_data;
  int page_kind = NIFT_SOURCE_TEXT;
  int tpl_kind = NIFT_SOURCE_TEXT;

  if (mode == 0) {
    Py_ssize_t nlen = 0;
    const char* n = PyUnicode_AsUTF8AndSize(a0, &nlen);
    if (n == nullptr) return nullptr;
    page_name.assign(n, (size_t)nlen);
  } else if (mode == 1) {
    if (!SourceFromObject(a0, &page_kind, &page_data)) {
      PyErr_SetString(PyExc_TypeError,
                      "render(page, template, ctx) expects str/bytes or (kind, data)");
      return nullptr;
    }
    if (!SourceFromObject(a1, &tpl_kind, &tpl_data)) {
      PyErr_SetString(PyExc_TypeError,
                      "render(page, template, ctx) expects str/bytes or (kind, data)");
      return nullptr;
    }
  } else {
    if (!SourceFromObject(a0, &page_kind, &page_data)) {
      PyErr_SetString(PyExc_TypeError,
                      "render_partial(partial, ctx) expects str/bytes or (kind, data)");
      return nullptr;
    }
  }

  // Strong references keep the Engine/Context alive during the render even if
  // the caller drops theirs on another thread; render_count defers close().
  Py_INCREF(engine_obj);
  if (ctx != nullptr) Py_INCREF(ctx_obj);
  self->render_count++;
  if (ctx != nullptr) ctx->render_count++;

  nift_render_result* result = nullptr;
  nift_status rc = NIFT_OK;
  nift_engine* engine = self->engine;
  nift_context* nctx = ctx != nullptr ? ctx->ctx : nullptr;

  Py_BEGIN_ALLOW_THREADS
  if (mode == 0) {
    rc = nift_engine_render_page(engine, nctx, page_name.data(), page_name.size(),
                                 &result);
  } else if (mode == 1) {
    nift_source page{static_cast<nift_source_kind>(page_kind), page_data.data(),
                     page_data.size(), nullptr, 0};
    nift_source tpl{static_cast<nift_source_kind>(tpl_kind), tpl_data.data(),
                    tpl_data.size(), nullptr, 0};
    rc = nift_engine_render(engine, &page, &tpl, nctx, &result);
  } else {
    nift_source partial{static_cast<nift_source_kind>(page_kind), page_data.data(),
                        page_data.size(), nullptr, 0};
    rc = nift_engine_render_partial(engine, &partial, nctx, &result);
  }
  Py_END_ALLOW_THREADS

  if (ctx != nullptr) {
    ctx->render_count--;
    if (ctx->disposed && ctx->render_count == 0) DestroyContext(ctx);
    Py_DECREF(ctx_obj);
  }
  self->render_count--;
  if (self->disposed && self->render_count == 0) DestroyEngine(self);
  Py_DECREF(engine_obj);

  if (rc != NIFT_OK) {
    if (result != nullptr) nift_render_result_free(result);
    PyErr_SetString(PyExc_RuntimeError, "render call failed");
    return nullptr;
  }

  PyObject* dict = PyDict_New();
  if (dict == nullptr) {
    nift_render_result_free(result);
    return nullptr;
  }
  int ok = nift_render_result_ok(result);
  PyObject* ok_obj = PyBool_FromLong(ok);
  PyDict_SetItemString(dict, "ok", ok_obj);
  Py_DECREF(ok_obj);
  if (ok) {
    nift_string output{nullptr, 0};
    nift_render_result_output(result, &output);
    PyObject* out_obj = StrFromNift(output);
    if (out_obj != nullptr) {
      PyDict_SetItemString(dict, "output", out_obj);
      Py_DECREF(out_obj);
    }

    PyObject* pagination = PyList_New(0);
    PyObject* deps = PyList_New(0);
    PyObject* reqs = PyList_New(0);
    size_t page_count = nift_render_result_pagination_count(result);
    for (size_t i = 0; i < page_count; ++i) {
      unsigned int page_no = 0;
      nift_string page_out{nullptr, 0};
      if (nift_render_result_pagination_get(result, i, &page_no, &page_out) == NIFT_OK) {
        PyObject* item = PyDict_New();
        PyObject* no = PyLong_FromUnsignedLong(page_no);
        PyObject* out = StrFromNift(page_out);
        PyDict_SetItemString(item, "page", no);
        PyDict_SetItemString(item, "output", out);
        PyList_Append(pagination, item);
        Py_DECREF(item);
        Py_DECREF(no);
        Py_DECREF(out);
      }
    }
    size_t dep_count = nift_render_result_dependency_count(result);
    for (size_t i = 0; i < dep_count; ++i) {
      nift_string d{nullptr, 0};
      if (nift_render_result_dependency_get(result, i, &d) == NIFT_OK) {
        PyObject* s = StrFromNift(d);
        PyList_Append(deps, s);
        Py_DECREF(s);
      }
    }
    size_t req_count = nift_render_result_requirement_count(result);
    for (size_t i = 0; i < req_count; ++i) {
      nift_string r{nullptr, 0};
      if (nift_render_result_requirement_get(result, i, &r) == NIFT_OK) {
        PyObject* s = StrFromNift(r);
        PyList_Append(reqs, s);
        Py_DECREF(s);
      }
    }
    PyDict_SetItemString(dict, "pagination", pagination);
    PyDict_SetItemString(dict, "dependencies", deps);
    PyDict_SetItemString(dict, "requirements", reqs);
    Py_DECREF(pagination);
    Py_DECREF(deps);
    Py_DECREF(reqs);
  } else {
    nift_string msg{nullptr, 0};
    nift_render_result_error_message(result, &msg);
    PyObject* m = StrFromNift(msg);
    if (m != nullptr) {
      PyDict_SetItemString(dict, "error", m);
      Py_DECREF(m);
    }
    nift_string src{nullptr, 0};
    nift_render_result_error_source(result, &src);
    PyObject* s = StrFromNift(src);
    if (s != nullptr) {
      PyDict_SetItemString(dict, "errorSource", s);
      Py_DECREF(s);
    }
    PyObject* line = PyLong_FromUnsignedLongLong(
        nift_render_result_error_line(result));
    PyObject* col = PyLong_FromUnsignedLongLong(
        nift_render_result_error_column(result));
    PyDict_SetItemString(dict, "errorLine", line);
    PyDict_SetItemString(dict, "errorColumn", col);
    Py_DECREF(line);
    Py_DECREF(col);
  }
  nift_render_result_free(result);
  return dict;
}

PyObject* EngineRenderPage(PyObject*, PyObject* args) { return DoRender(args, 0); }
PyObject* EngineRender(PyObject*, PyObject* args) { return DoRender(args, 1); }
PyObject* EngineRenderPartial(PyObject*, PyObject* args) { return DoRender(args, 2); }

// ---------------------------------------------------------------------------
// Context type
// ---------------------------------------------------------------------------

void ContextDealloc(PyObject* obj) {
  NiftContextObject* self = reinterpret_cast<NiftContextObject*>(obj);
  DestroyContext(self);
  Py_TYPE(self)->tp_free(obj);
}

PyObject* ContextNew(PyObject*, PyObject*) {
  NiftContextObject* self = PyObject_New(NiftContextObject, &NiftContextType);
  if (self == nullptr) return nullptr;
  self->ctx = nift_context_new();
  self->render_count = 0;
  self->disposed = 0;
  self->destroyed = 0;
  if (self->ctx == nullptr) {
    Py_DECREF(self);
    PyErr_SetString(PyExc_RuntimeError, "nift_context_new returned null");
    return nullptr;
  }
  return reinterpret_cast<PyObject*>(self);
}

NiftContextObject* CheckContext(PyObject* obj) {
  if (!PyObject_TypeCheck(obj, &NiftContextType)) {
    PyErr_SetString(PyExc_TypeError, "expected a Context");
    return nullptr;
  }
  NiftContextObject* self = reinterpret_cast<NiftContextObject*>(obj);
  if (self->disposed || self->destroyed) {
    PyErr_SetString(PyExc_RuntimeError, "Context has been disposed");
    return nullptr;
  }
  return self;
}

PyObject* ContextDispose(PyObject*, PyObject* args) {
  PyObject* obj = nullptr;
  if (!PyArg_ParseTuple(args, "O", &obj)) return nullptr;
  if (!PyObject_TypeCheck(obj, &NiftContextType)) {
    Py_RETURN_NONE;
  }
  NiftContextObject* self = reinterpret_cast<NiftContextObject*>(obj);
  self->disposed = 1;
  if (self->render_count == 0) DestroyContext(self);
  Py_RETURN_NONE;
}

PyObject* ContextSetString(PyObject*, PyObject* args) {
  PyObject* obj = nullptr;
  const char* name = nullptr;
  Py_ssize_t name_len = 0;
  const char* value = nullptr;
  Py_ssize_t value_len = 0;
  if (!PyArg_ParseTuple(args, "Os#s#", &obj, &name, &name_len, &value, &value_len))
    return nullptr;
  NiftContextObject* self = CheckContext(obj);
  if (self == nullptr) return nullptr;
  if (nift_context_set_string(self->ctx, name, (size_t)name_len, value,
                              (size_t)value_len) != NIFT_OK) {
    PyErr_Format(PyExc_RuntimeError, "invalid binding name: %s", name);
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject* ContextSetJSON(PyObject*, PyObject* args) {
  PyObject* obj = nullptr;
  const char* name = nullptr;
  Py_ssize_t name_len = 0;
  const char* json = nullptr;
  Py_ssize_t json_len = 0;
  if (!PyArg_ParseTuple(args, "Os#s#", &obj, &name, &name_len, &json, &json_len))
    return nullptr;
  NiftContextObject* self = CheckContext(obj);
  if (self == nullptr) return nullptr;
  if (nift_context_set_json(self->ctx, name, (size_t)name_len, json,
                            (size_t)json_len) != NIFT_OK) {
    PyErr_Format(PyExc_RuntimeError, "invalid binding name: %s", name);
    return nullptr;
  }
  Py_RETURN_NONE;
}

template <typename Fn>
PyObject* ContextScalar(PyObject* args, Fn fn) {
  PyObject* obj = nullptr;
  const char* name = nullptr;
  Py_ssize_t name_len = 0;
  PyObject* value = nullptr;
  if (!PyArg_ParseTuple(args, "Os#O", &obj, &name, &name_len, &value))
    return nullptr;
  NiftContextObject* self = CheckContext(obj);
  if (self == nullptr) return nullptr;
  nift_status rc = fn(self->ctx, name, (size_t)name_len, value);
  if (rc != NIFT_OK) {
    PyErr_Format(PyExc_RuntimeError, "invalid binding name: %s", name);
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject* ContextSetInt(PyObject*, PyObject* args) {
  return ContextScalar(args, [](nift_context* c, const char* n, size_t nl,
                                PyObject* v) {
    long iv = PyLong_AsLong(v);
    if (iv == -1 && PyErr_Occurred()) return NIFT_ERROR_INVALID_ARGUMENT;
    return nift_context_set_int(c, n, nl, (int32_t)iv);
  });
}

PyObject* ContextSetNumber(PyObject*, PyObject* args) {
  return ContextScalar(args, [](nift_context* c, const char* n, size_t nl,
                                PyObject* v) {
    double d = PyFloat_AsDouble(v);
    if (d == -1.0 && PyErr_Occurred()) return NIFT_ERROR_INVALID_ARGUMENT;
    return nift_context_set_number(c, n, nl, d);
  });
}

PyObject* ContextSetBool(PyObject*, PyObject* args) {
  return ContextScalar(args, [](nift_context* c, const char* n, size_t nl,
                                PyObject* v) {
    int b = PyObject_IsTrue(v);
    if (b < 0) return NIFT_ERROR_INVALID_ARGUMENT;
    return nift_context_set_bool(c, n, nl, b);
  });
}

PyObject* ContextSetPageName(PyObject*, PyObject* args) {
  PyObject* obj = nullptr;
  const char* name = nullptr;
  Py_ssize_t name_len = 0;
  if (!PyArg_ParseTuple(args, "Os#", &obj, &name, &name_len)) return nullptr;
  NiftContextObject* self = CheckContext(obj);
  if (self == nullptr) return nullptr;
  if (nift_context_set_page_name(self->ctx, name, (size_t)name_len) != NIFT_OK) {
    PyErr_SetString(PyExc_RuntimeError, "invalid context page name");
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject* ContextSetCurrentOutput(PyObject*, PyObject* args) {
  PyObject* obj = nullptr;
  const char* path = nullptr;
  Py_ssize_t path_len = 0;
  if (!PyArg_ParseTuple(args, "Os#", &obj, &path, &path_len)) return nullptr;
  NiftContextObject* self = CheckContext(obj);
  if (self == nullptr) return nullptr;
  if (nift_context_set_current_output(self->ctx, path, (size_t)path_len) !=
      NIFT_OK) {
    PyErr_SetString(PyExc_RuntimeError, "invalid context current output");
    return nullptr;
  }
  Py_RETURN_NONE;
}

// ---------------------------------------------------------------------------
// Type objects
// ---------------------------------------------------------------------------

PyTypeObject NiftEngineType = { PyVarObject_HEAD_INIT(nullptr, 0) };
PyTypeObject NiftContextType = { PyVarObject_HEAD_INIT(nullptr, 0) };

static void InitTypes() {
  NiftEngineType.tp_name = "nift._nift.NiftEngine";
  NiftEngineType.tp_basicsize = sizeof(NiftEngineObject);
  NiftEngineType.tp_flags = Py_TPFLAGS_DEFAULT;
  NiftEngineType.tp_doc = "Nift Engine native wrapper";
  NiftEngineType.tp_dealloc = EngineDealloc;

  NiftContextType.tp_name = "nift._nift.NiftContext";
  NiftContextType.tp_basicsize = sizeof(NiftContextObject);
  NiftContextType.tp_flags = Py_TPFLAGS_DEFAULT;
  NiftContextType.tp_doc = "Nift Context native wrapper";
  NiftContextType.tp_dealloc = ContextDealloc;
}

PyMethodDef kMethods[] = {
    {"new_engine", EngineNew, METH_NOARGS, "Create a standalone engine."},
    {"open_engine", EngineOpen, METH_VARARGS, "Open a project engine at root."},
    {"engine_close", EngineDispose, METH_VARARGS, "Dispose an engine."},
    {"engine_is_open", EngineIsOpen, METH_VARARGS, "Whether the engine is open."},
    {"engine_set_root", EngineSetRoot, METH_VARARGS, "Set the engine root."},
    {"engine_reload", EngineReload, METH_VARARGS, "Reload the project snapshot."},
    {"engine_set_string", EngineSetString, METH_VARARGS, "Set a string binding."},
    {"engine_set_int", EngineSetInt, METH_VARARGS, "Set an int binding."},
    {"engine_set_number", EngineSetNumber, METH_VARARGS, "Set a number binding."},
    {"engine_set_bool", EngineSetBool, METH_VARARGS, "Set a bool binding."},
    {"engine_set_json", EngineSetJSON, METH_VARARGS, "Set a JSON binding."},
    {"engine_set_loader", EngineSetLoader, METH_VARARGS, "Set the loader callback."},
    {"engine_set_environment_provider", EngineSetEnvironmentProvider, METH_VARARGS, "Set the environment provider."},
    {"engine_render_page", EngineRenderPage, METH_VARARGS, "Render a tracked page."},
    {"engine_render", EngineRender, METH_VARARGS, "Composed render."},
    {"engine_render_partial", EngineRenderPartial, METH_VARARGS, "Partial render."},
    {"new_context", ContextNew, METH_NOARGS, "Create a context."},
    {"context_close", ContextDispose, METH_VARARGS, "Dispose a context."},
    {"context_set_page_name", ContextSetPageName, METH_VARARGS, "Set page name."},
    {"context_set_current_output", ContextSetCurrentOutput, METH_VARARGS, "Set current output."},
    {"context_set_string", ContextSetString, METH_VARARGS, "Set a string binding."},
    {"context_set_int", ContextSetInt, METH_VARARGS, "Set an int binding."},
    {"context_set_number", ContextSetNumber, METH_VARARGS, "Set a number binding."},
    {"context_set_bool", ContextSetBool, METH_VARARGS, "Set a bool binding."},
    {"context_set_json", ContextSetJSON, METH_VARARGS, "Set a JSON binding."},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef kModule = {
    PyModuleDef_HEAD_INIT,
    "nift._nift",
    "Nift Embed native extension over the frozen C ABI.",
    -1,
    kMethods,
};

}  // namespace

PyMODINIT_FUNC PyInit__nift(void) {
  InitTypes();
  if (PyType_Ready(&NiftEngineType) < 0) return nullptr;
  if (PyType_Ready(&NiftContextType) < 0) return nullptr;
  PyObject* m = PyModule_Create(&kModule);
  if (m == nullptr) return nullptr;
  Py_INCREF(&NiftEngineType);
  PyModule_AddObject(m, "NiftEngine", reinterpret_cast<PyObject*>(&NiftEngineType));
  Py_INCREF(&NiftContextType);
  PyModule_AddObject(m, "NiftContext", reinterpret_cast<PyObject*>(&NiftContextType));
  return m;
}
