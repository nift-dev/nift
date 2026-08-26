// Package nift is the Go binding for the frozen Nift Embed C ABI.
//
// It is a thin, ownership-safe, concurrency-safe wrapper around the C ABI
// (include/nift/c_abi.h). It deliberately does NOT reimplement any Nift
// semantics: Go translates types, ownership and callbacks; parsing/rendering
// semantics live in the canonical C++ Embed engine behind the ABI.
//
// Ownership rule: anything borrowed from C is copied into Go-owned memory
// before the C lifetime can end. A Go Result returned from a render stays
// valid after the underlying C render result is destroyed, across subsequent
// renders and engine destruction.
//
// Callback rule: host providers (loader / environment) map to
// Found/NotFound/Error. A Go panic inside a callback is caught at the exported
// cgo boundary and becomes a controlled host failure (never unwound through C).
package nift

/*
#cgo CFLAGS: -I../../include
#cgo LDFLAGS: -L../.. -Wl,-Bstatic -lnift_c -Wl,-Bdynamic -lstdc++ -lm -pthread
#include <stdlib.h>
#include <string.h>
#include "nift/c_abi.h"

// Forward declarations for the //export callback bridges below; cgo provides
// their definitions from the exported Go functions.
nift_status niftGoLoader(void* user_data, char* path, size_t path_len, nift_string* out);
nift_status niftGoEnv(void* user_data, char* name, size_t name_len, nift_string* out);

// Shims with the exact nift_loader_callback / nift_environment_callback
// signatures (const char*) that delegate to the exports.
static inline nift_status nift_go_loader_shim(void* user_data, const char* path, size_t path_len, nift_string* out) {
    return niftGoLoader(user_data, (char*)path, path_len, out);
}
static inline nift_status nift_go_env_shim(void* user_data, const char* name, size_t name_len, nift_string* out) {
    return niftGoEnv(user_data, (char*)name, name_len, out);
}
static inline void nift_go_set_loader(nift_engine* engine, void* user_data) {
    nift_engine_set_loader(engine, nift_go_loader_shim, user_data);
}
static inline void nift_go_set_env(nift_engine* engine, void* user_data) {
    nift_engine_set_environment_provider(engine, nift_go_env_shim, user_data);
}
*/
import "C"

import (
	"errors"
	"fmt"
	"sync"
	"sync/atomic"
	"unsafe"
)

// HostStatus is the outcome of a host provider lookup.
type HostStatus int

const (
	// HostFound means a value (possibly empty) is supplied.
	HostFound HostStatus = iota
	// HostNotFound means the resource is absent / unset.
	HostNotFound
	// HostError means the host failed; the render reports a controlled error.
	HostError
)

// HostResult is the value / absent / error outcome of a host provider,
// mirroring the frozen Embed host-resource contract.
type HostResult struct {
	Status HostStatus
	Value  string
	Error  string
}

// HostLoader supplies a source for a path.
type HostLoader func(path string) HostResult

// HostEnvironment supplies a value for an environment variable name.
type HostEnvironment func(name string) HostResult

// RenderError is a failed render's diagnostic.
type RenderError struct {
	Message string
	Source  string
	Line    uint64
	Column  uint64
}

// Page is one paginated page beyond the primary (page numbers are 1-based).
type Page struct {
	Page   int    `json:"page"`
	Output string `json:"output"`
}

// Result is a fully Go-owned render outcome. It remains valid after the
// underlying C render result is destroyed.
type Result struct {
	OK           bool
	Output       string
	Error        *RenderError
	Dependencies []string
	Requirements []string
	Pages        []Page
}

// Context carries per-render page identity and bindings.
type Context struct {
	ctx *C.nift_context
	lifecycle  sync.Mutex
	closed     atomic.Bool
	renderCount atomic.Int32
}

// Engine is a thin handle over a C ABI engine. Concurrent renders on one
// Engine are safe; configuration (bindings, providers, root) must happen
// before concurrent use.
//
// Lifetime invariant (CP17): the lifecycle mutex couples render ADMISSION with
// quiescent reclamation, so "the render epoch reached zero" and "callback
// buffers are freed" cannot be separated by a new render starting. beginRender
// holds the lifecycle mutex while incrementing renderCount; the render-done
// closure holds it while decrementing and, at zero, reclaiming the callback
// buffers (or destroying the engine if Close was already called). Lock order
// is always lifecycle -> callbackSet.mu; callbacks take only callbackSet.mu,
// so there is no inversion.
type Engine struct {
	engine *C.nift_engine
	id     int64
	lifecycle sync.Mutex
	closed    atomic.Bool
	// renderCount tracks in-flight renders; when it returns to zero and no new
	// render can be admitted (we hold the lifecycle mutex), every callback
	// buffer is dead (the C ABI copies each callback `out` synchronously) and
	// is reclaimed. Bounded by peak concurrent render activity; NOT
	// free-on-next-callback.
	renderCount atomic.Int32
}

// ---------------------------------------------------------------------------
// callback bridge
// ---------------------------------------------------------------------------

var callbackRegistry sync.Map // int64 -> *callbackSet
var nextCallbackID atomic.Int64

type callbackSet struct {
	mu       sync.Mutex
	loader   HostLoader
	env      HostEnvironment
	token    unsafe.Pointer   // C-owned user_data token (C.malloc); C retains it
	bufs     []unsafe.Pointer // C-allocated callback-output buffers, reclaimed at each lifecycle-gated quiescent render epoch (or at Close for the final retained state)
}

func (c *callbackSet) freeAll() {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.token != nil {
		C.free(c.token)
		c.token = nil
	}
	c.freeBuffersLocked()
}

// freeBuffers releases all retained callback-output buffers. Safe only when
// no render is in flight (the caller guarantees it): every callback's `out`
// was copied synchronously by the C ABI before its render completed, so no
// buffer is still referenced by native code.
func (c *callbackSet) freeBuffers() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.freeBuffersLocked()
}

func (c *callbackSet) freeBuffersLocked() {
	for _, buf := range c.bufs {
		C.free(buf)
	}
	c.bufs = nil
}

// putC stores `value` into the callback output using a C-allocated buffer so no
// Go pointer ever crosses into C. The C++ engine copies the value synchronously
// during the callback; the buffer is then dead. The buffers are reclaimed as a
// single lifecycle-gated quiescent render epoch (renderCount returning to zero
// while holding Engine.lifecycle, so no new render can be admitted), bounding
// callback memory to peak concurrent render activity rather than the engine's
// whole lifetime; Close reclaims whatever final retained state remains.
func (c *callbackSet) putC(value string, out *C.nift_string) {
	if value == "" {
		out.data = nil
		out.length = 0
		return
	}
	buf := C.CBytes([]byte(value))
	c.mu.Lock()
	c.bufs = append(c.bufs, buf)
	c.mu.Unlock()
	out.data = (*C.char)(buf)
	out.length = C.size_t(len(value))
}

func (e *Engine) register() int64 {
	id := nextCallbackID.Add(1)
	// The user_data token is C-owned memory (C.malloc), NOT a Go heap object:
	// C retains it for the engine's lifetime and supplies it to callbacks from
	// any thread (including C++ pagination workers). C pointers may be freely
	// retained and passed back; Go heap pointers may not. The registry keeps
	// the Go closure state keyed by the integer id stored in the token, so C
	// never holds a Go pointer.
	token := C.malloc(C.size_t(unsafe.Sizeof(int64(0))))
	*(*int64)(token) = id
	callbackRegistry.Store(id, &callbackSet{token: token})
	e.id = id
	return id
}

// nativeOpTestHook, when set, is invoked inside a lifecycle-gated non-render
// native operation while the lifecycle mutex is held (after admission, before
// the native call). Test-only; used to force the "operation admitted, Close
// concurrently blocked" window deterministically.
var nativeOpTestHook func()

// reclaimTestHook, when set, is invoked inside the quiescent-reclamation
// critical section while the lifecycle mutex is held. Test-only; used to force
// the "epoch reached zero but reclamation in progress" window so a concurrent
// render admission attempt is exercised deterministically.
var reclaimTestHook func()

// beginRender admits a render. It holds the lifecycle mutex while
// incrementing renderCount, so a render can never be admitted between another
// render's zero-transition and its buffer reclamation. Rejects a closed
// engine. The returned done must run exactly once.
func (e *Engine) beginRender() (func(), error) {
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return nil, errors.New("Engine has been closed")
	}
	e.renderCount.Add(1)
	var once sync.Once
	done := func() {
		once.Do(func() {
			// Hold the lifecycle mutex across decrement and reclamation so no
			// new render can be admitted between "epoch reached zero" and the
			// buffers being freed (the CP17 quiescent-reclamation race).
			e.lifecycle.Lock()
			defer e.lifecycle.Unlock()
			if e.renderCount.Add(-1) == 0 {
				if reclaimTestHook != nil {
					reclaimTestHook()
				}
				if e.closed.Load() {
					e.destroyNowLocked()
				} else if v, ok := callbackRegistry.Load(e.id); ok {
					v.(*callbackSet).freeBuffers()
				}
			}
		})
	}
	return done, nil
}

// beginRenderCtx admits a render on a Context (lifecycle-gated, rejects a
// closed Context). Must be paired with endRenderCtx. Callers acquire the
// Engine lifecycle before the Context lifecycle (and release in reverse), so
// nested lifecycle locks never invert.
func (c *Context) beginRenderCtx() error {
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	if c.closed.Load() {
		return errors.New("Context has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	c.renderCount.Add(1)
	return nil
}

func (c *Context) endRenderCtx() {
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	if c.renderCount.Add(-1) == 0 && c.closed.Load() {
		c.destroyNowLocked()
	}
}

// destroyNowLocked frees the native engine, callback token and callback
// buffers. Must be called with the lifecycle mutex held and renderCount == 0
// (or as the final deferred destruction once the last render quiesces).
func (e *Engine) destroyNowLocked() {
	if e.id != 0 {
		if v, ok := callbackRegistry.LoadAndDelete(e.id); ok {
			v.(*callbackSet).freeAll()
		}
		e.id = 0
	}
	if e.engine != nil {
		C.nift_engine_free(e.engine)
		e.engine = nil
	}
}

func (c *Context) destroyNowLocked() {
	if c.ctx != nil {
		C.nift_context_free(c.ctx)
		c.ctx = nil
	}
}

func (e *Engine) unregister() {
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	e.destroyNowLocked()
}

func hostResultToC(cb *callbackSet, res HostResult, out *C.nift_string) C.nift_status {
	switch res.Status {
	case HostFound:
		cb.putC(res.Value, out)
		return C.NIFT_OK
	case HostNotFound:
		out.data = nil
		out.length = 0
		return C.NIFT_ERROR_NOT_FOUND
	default:
		// HostError: place the diagnostic into `out`; the C ABI bridge copies it
		// synchronously and uses it as the failed RenderResult diagnostic (an
		// empty diagnostic falls back to the generic "host callback failed").
		cb.putC(res.Error, out)
		return C.NIFT_ERROR_CALLBACK
	}
}

// invokeLoader is the shared body for the exported loader callback; it contains
// panics so a Go panic never unwinds through C.
func invokeLoader(id int64, path string, out *C.nift_string) (status C.nift_status) {
	defer func() {
		if r := recover(); r != nil {
			out.data = nil
			out.length = 0
			status = C.NIFT_ERROR_CALLBACK
		}
	}()
	v, ok := callbackRegistry.Load(id)
	if !ok {
		return C.NIFT_ERROR_CALLBACK
	}
	cb := v.(*callbackSet)
	if cb.loader == nil {
		return C.NIFT_ERROR_NOT_FOUND
	}
	return hostResultToC(cb, cb.loader(path), out)
}

// invokeEnvironment is the shared body for the exported environment callback.
func invokeEnvironment(id int64, name string, out *C.nift_string) (status C.nift_status) {
	defer func() {
		if r := recover(); r != nil {
			out.data = nil
			out.length = 0
			status = C.NIFT_ERROR_CALLBACK
		}
	}()
	v, ok := callbackRegistry.Load(id)
	if !ok {
		return C.NIFT_ERROR_CALLBACK
	}
	cb := v.(*callbackSet)
	if cb.env == nil {
		return C.NIFT_ERROR_NOT_FOUND
	}
	return hostResultToC(cb, cb.env(name), out)
}

//export niftGoLoader
func niftGoLoader(userData unsafe.Pointer, path *C.char, pathLen C.size_t, out *C.nift_string) C.nift_status {
	key := C.GoStringN(path, C.int(pathLen))
	return invokeLoader(*(*int64)(userData), key, out)
}

//export niftGoEnv
func niftGoEnv(userData unsafe.Pointer, name *C.char, nameLen C.size_t, out *C.nift_string) C.nift_status {
	key := C.GoStringN(name, C.int(nameLen))
	return invokeEnvironment(*(*int64)(userData), key, out)
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

// NewEngine creates a standalone engine (deterministic; never walks the
// filesystem).
func NewEngine() *Engine {
	e := &Engine{engine: C.nift_engine_new()}
	e.register()
	return e
}

// OpenEngine creates a project-aware engine for the Nift project at root.
// Check IsOpen / OpenError afterwards.
func OpenEngine(root string) *Engine {
	r := C.CString(root)
	defer C.free(unsafe.Pointer(r))
	e := &Engine{engine: C.nift_engine_open(r, C.size_t(len(root)))}
	e.register()
	return e
}

// Close logically disposes the engine: new operations are rejected
// immediately, but native destruction (engine, callback token, callback
// buffers) is deferred until the last in-flight render quiesces, so a render
// on another goroutine can never use freed native state. Close is idempotent.
func (e *Engine) Close() {
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	e.closed.Store(true)
	if e.renderCount.Load() == 0 {
		e.destroyNowLocked()
	}
}

// IsOpen reports whether a project snapshot is loaded. Holds the lifecycle
// mutex across the native call so a concurrent Close cannot free the engine
// mid-call; a closed engine reports false.
func (e *Engine) IsOpen() bool {
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return false
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return C.nift_engine_is_open(e.engine) != 0
}

// OpenError returns the project-open diagnostic (empty when open). Holds the
// lifecycle mutex across the native call; a closed engine returns "".
func (e *Engine) OpenError() string {
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return ""
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	var s C.nift_string
	if C.nift_engine_open_error(e.engine, &s) != C.NIFT_OK {
		return ""
	}
	return C.GoStringN(s.data, C.int(s.length))
}

// SetRoot sets the base directory for relative source resolution. Holds the
// lifecycle mutex across the native call so a concurrent Close cannot free
// the engine mid-call.
func (e *Engine) SetRoot(root string) {
	r := C.CString(root)
	defer C.free(unsafe.Pointer(r))
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	C.nift_engine_set_root(e.engine, r, C.size_t(len(root)))
}

// Reload atomically replaces the project snapshot. It returns a Go error only
// for mechanical failures; a failed reload returns a non-nil error carrying the
// diagnostic and keeps the last known-good generation in service.
func (e *Engine) Reload() error {
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return errors.New("Engine has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	var s C.nift_string
	status := C.nift_engine_reload(e.engine, &s)
	if status == C.NIFT_OK {
		return nil
	}
	if status == C.NIFT_ERROR_PROJECT {
		msg := C.GoStringN(s.data, C.int(s.length))
		return fmt.Errorf("reload: %s", msg)
	}
	return abiStatusError(status)
}

// SetLoader installs a loader provider. Engine mutation is not thread-safe
// with active renders.
func (e *Engine) SetLoader(loader HostLoader) {
	// Provider installation is a single lifecycle-protected operation: the
	// closed check, the e.id read, the registry lookup, the callback-state
	// mutation and the native install all happen under the lifecycle mutex, so
	// Close can never free the registry entry / engine between them.
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	v, ok := callbackRegistry.Load(e.id)
	if !ok {
		return
	}
	set := v.(*callbackSet)
	set.loader = loader
	if loader == nil {
		C.nift_engine_set_loader(e.engine, nil, nil)
		return
	}
	C.nift_go_set_loader(e.engine, set.token)
}

// SetEnvironmentProvider installs an environment provider.
func (e *Engine) SetEnvironmentProvider(env HostEnvironment) {
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	v, ok := callbackRegistry.Load(e.id)
	if !ok {
		return
	}
	set := v.(*callbackSet)
	set.env = env
	if env == nil {
		C.nift_engine_set_environment_provider(e.engine, nil, nil)
		return
	}
	C.nift_go_set_env(e.engine, set.token)
}

func goString(s string) (*C.char, C.size_t) {
	if s == "" {
		return nil, 0
	}
	return (*C.char)(unsafe.Pointer(unsafe.StringData(s))), C.size_t(len(s))
}

func bindingNameError(status C.nift_status) error {
	if status != C.NIFT_OK {
		return abiStatusError(status)
	}
	return nil
}

// SetString sets a long-lived default string binding.
func (e *Engine) SetString(name, value string) error {
	n, nl := goString(name)
	v, vl := goString(value)
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return errors.New("Engine has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return bindingNameError(C.nift_engine_set_string(e.engine, n, nl, v, vl))
}

// SetInt sets a long-lived default int32 binding.
func (e *Engine) SetInt(name string, value int32) error {
	n, nl := goString(name)
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return errors.New("Engine has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return bindingNameError(C.nift_engine_set_int(e.engine, n, nl, C.int32_t(value)))
}

// SetNumber sets a long-lived default double binding.
func (e *Engine) SetNumber(name string, value float64) error {
	n, nl := goString(name)
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return errors.New("Engine has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return bindingNameError(C.nift_engine_set_number(e.engine, n, nl, C.double(value)))
}

// SetBool sets a long-lived default boolean binding.
func (e *Engine) SetBool(name string, value bool) error {
	n, nl := goString(name)
	v := C.int(0)
	if value {
		v = 1
	}
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return errors.New("Engine has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return bindingNameError(C.nift_engine_set_bool(e.engine, n, nl, v))
}

// SetJSON sets a long-lived default JSON binding. Malformed JSON is a Go error.
func (e *Engine) SetJSON(name, json string) error {
	n, nl := goString(name)
	j, jl := goString(json)
	e.lifecycle.Lock()
	defer e.lifecycle.Unlock()
	if e.closed.Load() {
		return errors.New("Engine has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return bindingNameError(C.nift_engine_set_json(e.engine, n, nl, j, jl))
}

// renderFrame admits the render on the Engine (lifecycle-gated) and, when a
// Context is supplied, on the Context too. The Engine lifecycle is acquired
// before the Context lifecycle and released after it, so nested lifecycle
// locks never invert across concurrent renders.
func (e *Engine) renderFrame(ctx *Context, fn func() (Result, error)) (Result, error) {
	done, err := e.beginRender()
	if err != nil {
		return Result{}, err
	}
	defer done()
	if ctx != nil {
		if err := ctx.beginRenderCtx(); err != nil {
			return Result{}, err
		}
		defer ctx.endRenderCtx()
	}
	return fn()
}

// RenderPage renders a tracked page by name with complete pagination. A host
// failure is a rendering outcome (Result.OK == false with the diagnostic); the
// returned error is reserved for mechanical ABI failures.
func (e *Engine) RenderPage(name string, ctx *Context) (Result, error) {
	return e.renderFrame(ctx, func() (Result, error) {
		n, nl := goString(name)
		var r *C.nift_render_result
		status := C.nift_engine_render_page(e.engine, ctx.ptr(), n, nl, &r)
		if status != C.NIFT_OK {
			return Result{}, abiStatusError(status)
		}
		defer C.nift_render_result_free(r)
		return convertResult(r), nil
	})
}

// RenderSource is a page/template input: either in-memory text or a path
// resolved against the engine root (mirrors the C ABI nift_source).
type RenderSource struct {
	IsPath bool
	Text   string
	Path   string
}

func sourceToC(src RenderSource) C.nift_source {
	if src.IsPath {
		p, pl := goString(src.Path)
		return C.nift_source{kind: C.NIFT_SOURCE_PATH, data: p, length: pl}
	}
	t, tl := goString(src.Text)
	return C.nift_source{kind: C.NIFT_SOURCE_TEXT, data: t, length: tl}
}

// RenderSources composes arbitrary page/template sources (text or path).
func (e *Engine) RenderSources(page, template RenderSource, ctx *Context) (Result, error) {
	return e.renderFrame(ctx, func() (Result, error) {
		pageSrc := sourceToC(page)
		tplSrc := sourceToC(template)
		var r *C.nift_render_result
		status := C.nift_engine_render(e.engine, &pageSrc, &tplSrc, ctx.ptr(), &r)
		if status != C.NIFT_OK {
			return Result{}, abiStatusError(status)
		}
		defer C.nift_render_result_free(r)
		return convertResult(r), nil
	})
}

// Render composes a page and template (both text). The template must contain
// exactly one @content.
func (e *Engine) Render(page, template string, ctx *Context) (Result, error) {
	return e.renderFrame(ctx, func() (Result, error) {
		pageSrc := C.nift_source{kind: C.NIFT_SOURCE_TEXT}
		pageSrc.data, pageSrc.length = goString(page)
		tplSrc := C.nift_source{kind: C.NIFT_SOURCE_TEXT}
		tplSrc.data, tplSrc.length = goString(template)
		var r *C.nift_render_result
		status := C.nift_engine_render(e.engine, &pageSrc, &tplSrc, ctx.ptr(), &r)
		if status != C.NIFT_OK {
			return Result{}, abiStatusError(status)
		}
		defer C.nift_render_result_free(r)
		return convertResult(r), nil
	})
}

// RenderPath composes a page and template loaded from paths resolved against
// the engine root. Used for loader-backed and on-disk path renders.
func (e *Engine) RenderPath(pagePath, templatePath string, ctx *Context) (Result, error) {
	return e.RenderSources(
		RenderSource{IsPath: true, Path: pagePath},
		RenderSource{IsPath: true, Path: templatePath},
		ctx,
	)
}

// RenderPartial renders a standalone partial/fragment. A partial containing
// @content is an error (Result.OK == false).
func (e *Engine) RenderPartial(page string, ctx *Context) (Result, error) {
	return e.renderFrame(ctx, func() (Result, error) {
		src := C.nift_source{kind: C.NIFT_SOURCE_TEXT}
		src.data, src.length = goString(page)
		var r *C.nift_render_result
		status := C.nift_engine_render_partial(e.engine, &src, ctx.ptr(), &r)
		if status != C.NIFT_OK {
			return Result{}, abiStatusError(status)
		}
		defer C.nift_render_result_free(r)
		return convertResult(r), nil
	})
}

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------

// NewContext creates a per-render context.
func NewContext() *Context {
	return &Context{ctx: C.nift_context_new()}
}

// Close destroys the context.
// Close logically disposes the Context: new use is rejected immediately, but
// the native context is freed only after the last in-flight render using it
// quiesces. Close is idempotent.
func (c *Context) Close() {
	if c == nil {
		return
	}
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	c.closed.Store(true)
	if c.renderCount.Load() == 0 {
		c.destroyNowLocked()
	}
}

func (c *Context) ptr() *C.nift_context {
	if c == nil {
		return nil
	}
	return c.ctx
}

// SetPageName sets the page identity.
func (c *Context) SetPageName(name string) {
	n, nl := goString(name)
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	if c.closed.Load() {
		return
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	C.nift_context_set_page_name(c.ctx, n, nl)
}

// SetCurrentOutput sets the generated output location used by @pathto.
func (c *Context) SetCurrentOutput(path string) {
	p, pl := goString(path)
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	if c.closed.Load() {
		return
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	C.nift_context_set_current_output(c.ctx, p, pl)
}

// SetTitle sets the per-render title.
func (c *Context) SetTitle(title string) {
	t, tl := goString(title)
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	if c.closed.Load() {
		return
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	C.nift_context_set_title(c.ctx, t, tl)
}

// SetString sets a request-scoped string binding.
func (c *Context) SetString(name, value string) error {
	n, nl := goString(name)
	v, vl := goString(value)
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	if c.closed.Load() {
		return errors.New("Context has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return bindingNameError(C.nift_context_set_string(c.ctx, n, nl, v, vl))
}

// SetInt sets a request-scoped int32 binding.
func (c *Context) SetInt(name string, value int32) error {
	n, nl := goString(name)
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	if c.closed.Load() {
		return errors.New("Context has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return bindingNameError(C.nift_context_set_int(c.ctx, n, nl, C.int32_t(value)))
}

// SetNumber sets a request-scoped double binding.
func (c *Context) SetNumber(name string, value float64) error {
	n, nl := goString(name)
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	if c.closed.Load() {
		return errors.New("Context has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return bindingNameError(C.nift_context_set_number(c.ctx, n, nl, C.double(value)))
}

// SetBool sets a request-scoped boolean binding.
func (c *Context) SetBool(name string, value bool) error {
	n, nl := goString(name)
	v := C.int(0)
	if value {
		v = 1
	}
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	if c.closed.Load() {
		return errors.New("Context has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return bindingNameError(C.nift_context_set_bool(c.ctx, n, nl, v))
}

// SetJSON sets a request-scoped JSON binding.
func (c *Context) SetJSON(name, json string) error {
	n, nl := goString(name)
	j, jl := goString(json)
	c.lifecycle.Lock()
	defer c.lifecycle.Unlock()
	if c.closed.Load() {
		return errors.New("Context has been closed")
	}
	if nativeOpTestHook != nil {
		nativeOpTestHook()
	}
	return bindingNameError(C.nift_context_set_json(c.ctx, n, nl, j, jl))
}

// ---------------------------------------------------------------------------
// result conversion (all copies; the Go Result owns its memory)
// ---------------------------------------------------------------------------

func cString(s C.nift_string) string {
	if s.data == nil || s.length == 0 {
		return ""
	}
	return C.GoStringN(s.data, C.int(s.length))
}

func convertResult(r *C.nift_render_result) Result {
	out := Result{OK: C.nift_render_result_ok(r) != 0}
	if out.OK {
		var o C.nift_string
		C.nift_render_result_output(r, &o)
		out.Output = cString(o)
	} else {
		out.Error = &RenderError{}
		var m, src C.nift_string
		C.nift_render_result_error_message(r, &m)
		C.nift_render_result_error_source(r, &src)
		out.Error.Message = cString(m)
		out.Error.Source = cString(src)
		out.Error.Line = uint64(C.nift_render_result_error_line(r))
		out.Error.Column = uint64(C.nift_render_result_error_column(r))
	}

	depCount := int(C.nift_render_result_dependency_count(r))
	if depCount > 0 {
		out.Dependencies = make([]string, 0, depCount)
		for i := 0; i < depCount; i++ {
			var d C.nift_string
			C.nift_render_result_dependency_get(r, C.size_t(i), &d)
			out.Dependencies = append(out.Dependencies, cString(d))
		}
	}
	reqCount := int(C.nift_render_result_requirement_count(r))
	if reqCount > 0 {
		out.Requirements = make([]string, 0, reqCount)
		for i := 0; i < reqCount; i++ {
			var q C.nift_string
			C.nift_render_result_requirement_get(r, C.size_t(i), &q)
			out.Requirements = append(out.Requirements, cString(q))
		}
	}
	pageCount := int(C.nift_render_result_pagination_count(r))
	if pageCount > 0 {
		out.Pages = make([]Page, 0, pageCount)
		for i := 0; i < pageCount; i++ {
			var pageNo C.uint
			var po C.nift_string
			C.nift_render_result_pagination_get(r, C.size_t(i), &pageNo, &po)
			out.Pages = append(out.Pages, Page{Page: int(pageNo), Output: cString(po)})
		}
	}
	return out
}

func abiStatusError(status C.nift_status) error {
	switch status {
	case C.NIFT_ERROR_INVALID_ARGUMENT:
		return fmt.Errorf("nift: invalid argument")
	case C.NIFT_ERROR_PROJECT:
		return fmt.Errorf("nift: project error")
	case C.NIFT_ERROR_INTERNAL:
		return fmt.Errorf("nift: internal error")
	default:
		return fmt.Errorf("nift: abi status %d", int(status))
	}
}

// ABICompat reports whether the loaded C ABI version is compatible with this
// binding.
func ABICompat() error {
	major := int(C.nift_abi_version_major())
	minor := int(C.nift_abi_version_minor())
	if major != 1 || minor != 0 {
		return fmt.Errorf("nift: unsupported ABI version %d.%d", major, minor)
	}
	return nil
}
