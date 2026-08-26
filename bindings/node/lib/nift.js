// Nift Embed Node binding - idiomatic JavaScript wrapper over the N-API addon.
//
// Threading model: renders are asynchronous (they run on a native worker
// thread through napi_async_work) so the JS event loop stays free to service
// the loader/environment host callbacks, which run on the JS thread. Host
// callbacks must therefore return synchronously: return a string (Found),
// null/undefined (NotFound), or throw (Error with the diagnostic).
"use strict";

const native = require("../build/nift_node.node");

function checkArg(value, type, what) {
  if (value === undefined || value === null) return;
  if (typeof value !== type) {
    throw new TypeError(`Nift: ${what} must be a ${type}`);
  }
}

function checkSource(value, what) {
  if (value === undefined || value === null) return value;
  if (typeof value === "string") return value;
  if (typeof value === "object" && value !== null) {
    if (typeof value.path === "string" || typeof value.text === "string") return value;
  }
  throw new TypeError(`Nift: ${what} must be a string or a {path|text} object`);
}

class Engine {
  constructor(handle) {
    this._handle = handle;
    this._disposed = false;
  }

  static new() {
    return new Engine(native.newEngine());
  }

  static open(root) {
    checkArg(root, "string", "root");
    return new Engine(native.openEngine(root));
  }

  _check() {
    if (this._disposed) {
      throw new Error("Nift: Engine has been disposed");
    }
    return this;
  }

  close() {
    if (this._disposed) return;
    this._disposed = true;
    native.disposeEngine.call(this._handle);
  }

  isOpen() {
    return native.engineIsOpen.call(this._handle);
  }

  setRoot(root) {
    this._check();
    native.engineSetRoot.call(this._handle, String(root));
    return this;
  }

  reload() {
    this._check();
    native.engineReload.call(this._handle);
    return this;
  }

  setString(name, value) {
    this._check();
    native.engineSetString.call(this._handle, String(name), String(value));
    return this;
  }

  setInt(name, value) {
    this._check();
    native.engineSetInt.call(this._handle, String(name), value);
    return this;
  }

  setNumber(name, value) {
    this._check();
    native.engineSetNumber.call(this._handle, String(name), value);
    return this;
  }

  setBool(name, value) {
    this._check();
    native.engineSetBool.call(this._handle, String(name), !!value);
    return this;
  }

  setJSON(name, json) {
    this._check();
    native.engineSetJSON.call(this._handle, String(name), String(json));
    return this;
  }

  setLoader(fn) {
    this._check();
    if (fn !== null && fn !== undefined && typeof fn !== "function") {
      throw new TypeError("Nift: setLoader requires a function or null");
    }
    native.engineSetLoader.call(this._handle, fn || null);
    return this;
  }

  setEnvironmentProvider(fn) {
    this._check();
    if (fn !== null && fn !== undefined && typeof fn !== "function") {
      throw new TypeError("Nift: setEnvironmentProvider requires a function or null");
    }
    native.engineSetEnvironmentProvider.call(this._handle, fn || null);
    return this;
  }

  renderPage(pageName, ctx) {
    this._check();
    checkArg(pageName, "string", "pageName");
    return native.engineRenderPage.call(this._handle, pageName, ctx ? ctx._handle : null);
  }

  render(page, template, ctx) {
    this._check();
    return native.engineRender.call(
      this._handle,
      checkSource(page, "page"),
      checkSource(template, "template"),
      ctx ? ctx._handle : null
    );
  }

  renderPartial(partial, ctx) {
    this._check();
    return native.engineRenderPartial.call(
      this._handle,
      checkSource(partial, "partial"),
      ctx ? ctx._handle : null
    );
  }
}

class Context {
  constructor(handle) {
    this._handle = handle !== undefined ? handle : native.newContext();
    this._disposed = false;
  }

  static new() {
    return new Context(native.newContext());
  }

  _check() {
    if (this._disposed) {
      throw new Error("Nift: Context has been disposed");
    }
    return this;
  }

  close() {
    if (this._disposed) return;
    this._disposed = true;
    native.disposeContext.call(this._handle);
  }

  setPageName(name) {
    this._check();
    native.contextSetPageName.call(this._handle, String(name));
    return this;
  }

  setCurrentOutput(path) {
    this._check();
    native.contextSetCurrentOutput.call(this._handle, String(path));
    return this;
  }

  setString(name, value) {
    this._check();
    native.contextSetString.call(this._handle, String(name), String(value));
    return this;
  }

  setInt(name, value) {
    this._check();
    native.contextSetInt.call(this._handle, String(name), value);
    return this;
  }

  setNumber(name, value) {
    this._check();
    native.contextSetNumber.call(this._handle, String(name), value);
    return this;
  }

  setBool(name, value) {
    this._check();
    native.contextSetBool.call(this._handle, String(name), !!value);
    return this;
  }

  setJSON(name, json) {
    this._check();
    native.contextSetJSON.call(this._handle, String(name), String(json));
    return this;
  }
}

module.exports = { Engine, Context };
