# Nift Embed Python binding - idiomatic wrapper over the native extension.
#
# GIL / threading model: renders are synchronous; the native extension releases
# the GIL during the C++ render so loader/environment host callbacks (which may
# fire from C++ pagination worker threads) can re-acquire it via PyGILState.
# Callbacks must return synchronously: a str/bytes (Found), None (NotFound), or
# raise (Error whose message is the diagnostic).
#
# Lifetime: close() rejects new operations immediately but defers native
# destruction until in-flight renders quiesce (enforced invariant; a render
# holds a strong reference to the Engine/Context for its duration).
from . import _nift

__all__ = ["Engine", "Context", "RenderResult"]


class RenderResult:
    """The outcome of a render: ok/output on success, error fields on failure,
    plus pagination (pages 2..N), dependencies and requirements."""

    __slots__ = (
        "ok",
        "output",
        "error",
        "error_source",
        "error_line",
        "error_column",
        "pagination",
        "dependencies",
        "requirements",
    )

    def __init__(self, raw):
        self.ok = bool(raw.get("ok"))
        self.output = raw.get("output", "")
        self.error = raw.get("error")
        self.error_source = raw.get("errorSource", "")
        self.error_line = raw.get("errorLine", 0)
        self.error_column = raw.get("errorColumn", 0)
        self.pagination = raw.get("pagination", [])
        self.dependencies = raw.get("dependencies", [])
        self.requirements = raw.get("requirements", [])

    def __repr__(self):
        if self.ok:
            return f"<RenderResult ok output={self.output!r}>"
        return f"<RenderResult failed error={self.error!r}>"


def _source(value, what):
    """Accept str/bytes (in-memory text) or an object/dict with path/text."""
    if isinstance(value, str):
        return (0, value.encode("utf-8"))
    if isinstance(value, bytes):
        return (0, value)
    if isinstance(value, dict):
        if "path" in value:
            return (1, value["path"])
        if "text" in value:
            return (0, value["text"])
    elif value is not None:
        if hasattr(value, "path"):
            return (1, value.path)
        if hasattr(value, "text"):
            return (0, value.text)
    raise TypeError(f"Nift: {what} must be a str, bytes, or a {{path|text}} object")


class Engine:
    """A Nift Embed engine (thin adapter over the frozen C ABI)."""

    __slots__ = ("_handle", "_disposed")

    def __init__(self, handle):
        self._handle = handle
        self._disposed = False

    @staticmethod
    def new():
        return Engine(_nift.new_engine())

    @staticmethod
    def open(root):
        return Engine(_nift.open_engine(str(root)))

    def _check(self):
        if self._disposed:
            raise RuntimeError("Engine has been disposed")
        return self

    def close(self):
        if self._disposed:
            return
        self._disposed = True
        _nift.engine_close(self._handle)

    def is_open(self):
        return bool(_nift.engine_is_open(self._handle))

    def set_root(self, root):
        self._check()
        _nift.engine_set_root(self._handle, str(root))
        return self

    def reload(self):
        self._check()
        _nift.engine_reload(self._handle)
        return self

    def set_string(self, name, value):
        self._check()
        _nift.engine_set_string(self._handle, str(name), str(value))
        return self

    def set_int(self, name, value):
        self._check()
        _nift.engine_set_int(self._handle, str(name), int(value))
        return self

    def set_number(self, name, value):
        self._check()
        _nift.engine_set_number(self._handle, str(name), float(value))
        return self

    def set_bool(self, name, value):
        self._check()
        _nift.engine_set_bool(self._handle, str(name), bool(value))
        return self

    def set_json(self, name, json):
        self._check()
        _nift.engine_set_json(self._handle, str(name), str(json))
        return self

    def set_loader(self, fn=None):
        self._check()
        if fn is not None and not callable(fn):
            raise TypeError("set_loader requires a callable or None")
        _nift.engine_set_loader(self._handle, fn if fn is not None else None)
        return self

    def set_environment_provider(self, fn=None):
        self._check()
        if fn is not None and not callable(fn):
            raise TypeError("set_environment_provider requires a callable or None")
        _nift.engine_set_environment_provider(self._handle, fn if fn is not None else None)
        return self

    def render(self, page_name, ctx=None):
        """Render a tracked project page by name. The name is ALWAYS a tracked
        page name - never a filesystem path or literal template source; an
        unknown tracked name is a controlled unknown-page error. ctx omitted is
        a fresh empty context."""
        self._check()
        return RenderResult(
            _nift.engine_render_page(self._handle, str(page_name), ctx._handle if ctx else None)
        )

    def render_path(self, path, ctx=None):
        """Render a standalone filesystem source (the file at path) as a
        partial. The path is ALWAYS a filesystem path: a missing path is a
        controlled missing-path error and is never reinterpreted as literal
        template text."""
        self._check()
        return RenderResult(
            _nift.engine_render_path(self._handle, str(path), ctx._handle if ctx else None)
        )

    def render_text(self, text, ctx=None):
        """Render the supplied bytes as a standalone in-memory source
        (partial). The text is ALWAYS template source: it is never checked
        against the filesystem, so it cannot be misinterpreted as a page or
        path name."""
        self._check()
        return RenderResult(
            _nift.engine_render_text(self._handle, str(text), ctx._handle if ctx else None)
        )

    def render_sources(self, page, template, ctx=None):
        """Full page + template composition with explicit, typed sources
        (strings or (kind, data) tuples, never inferred from the filesystem).
        The template must contain exactly one @content."""
        self._check()
        return RenderResult(
            _nift.engine_render(
                self._handle,
                _source(page, "page"),
                _source(template, "template"),
                ctx._handle if ctx else None,
            )
        )


class Context:
    """Per-render request state (bindings, page name, current output)."""

    __slots__ = ("_handle", "_disposed")

    def __init__(self, handle=None):
        self._handle = handle if handle is not None else _nift.new_context()
        self._disposed = False

    def _check(self):
        if self._disposed:
            raise RuntimeError("Context has been disposed")
        return self

    def close(self):
        if self._disposed:
            return
        self._disposed = True
        _nift.context_close(self._handle)

    def set_page_name(self, name):
        self._check()
        _nift.context_set_page_name(self._handle, str(name))
        return self

    def set_current_output(self, path):
        self._check()
        _nift.context_set_current_output(self._handle, str(path))
        return self

    def set_string(self, name, value):
        self._check()
        _nift.context_set_string(self._handle, str(name), str(value))
        return self

    def set_int(self, name, value):
        self._check()
        _nift.context_set_int(self._handle, str(name), int(value))
        return self

    def set_number(self, name, value):
        self._check()
        _nift.context_set_number(self._handle, str(name), float(value))
        return self

    def set_bool(self, name, value):
        self._check()
        _nift.context_set_bool(self._handle, str(name), bool(value))
        return self

    def set_json(self, name, json):
        self._check()
        _nift.context_set_json(self._handle, str(name), str(json))
        return self
