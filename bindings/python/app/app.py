# Nift Python HTTP dogfood: a small genuine WSGI application using Nift Embed.
#
# Python owns HTTP (wsgiref); Nift owns template rendering. A long-lived
# project Engine serves page/pagination renders, an environment provider
# supplies resource values, a loader-backed render exercises the seam, and the
# error paths return 500 with the verbatim diagnostic. The engine is disposed
# on shutdown.
import json
import os
import threading
from wsgiref.simple_server import make_server

from nift import Context, Engine

HERE = os.path.dirname(os.path.abspath(__file__))
SITE_ROOT = os.path.join(HERE, "site")
PORT = int(os.environ.get("PORT", "5201"))

engine = Engine.open(SITE_ROOT)
engine.set_string("site", "Nift Python Dogfood")


def env_provider(name):
    if name == "SITE_VERSION":
        return "1.0"
    if name == "FORCE_ERROR":
        raise RuntimeError("forced host error")
    return None


engine.set_environment_provider(env_provider)


def render_home():
    ctx = Context()
    ctx.set_string("who", "world")
    try:
        return engine.render_page("home", ctx)
    finally:
        ctx.close()


def render_posts():
    return engine.render_page("blog")


def render_partial():
    pe = Engine.new()
    pe.set_root("/")
    pe.set_loader(lambda p: "<p>from loader</p>\n" if p.endswith("/greeting.html") else None)
    try:
        return pe.render('@input("greeting.html")', "<main>@content</main>")
    finally:
        pe.close()


def render_concurrency():
    failures = 0
    results = []

    def work(i):
        ctx = Context()
        ctx.set_string("who", "c%d" % i)
        try:
            r = engine.render_page("home", ctx)
            if not r.ok:
                failures += 1  # noqa: B023
        finally:
            ctx.close()

    threads = [threading.Thread(target=work, args=(i,)) for i in range(32)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return {"rendered": 32, "failures": failures}


def render_error():
    ctx = Context()
    try:
        return engine.render("@getenv(FORCE_ERROR)", "<main>@content</main>", ctx)
    finally:
        ctx.close()


def render_malformed():
    ctx = Context()
    try:
        return engine.render('@json("content/bad.json", d)$[d.x]', "<main>@content</main>", ctx)
    finally:
        ctx.close()


def app(environ, start_response):
    path = environ.get("PATH_INFO", "/")
    try:
        if path == "/":
            r = render_home()
            if not r.ok:
                start_response("500 Internal Server Error", [("Content-Type", "text/plain")])
                return [("render failed: %s" % r.error).encode("utf-8")]
            start_response("200 OK", [("Content-Type", "text/html; charset=utf-8")])
            return [r.output.encode("utf-8")]
        if path == "/posts":
            r = render_posts()
            if not r.ok:
                start_response("500 Internal Server Error", [("Content-Type", "text/plain")])
                return [("render failed: %s" % r.error).encode("utf-8")]
            body = json.dumps({
                "ok": r.ok,
                "output": r.output,
                "pages": r.pagination,
                "dependencies": r.dependencies,
            })
            start_response("200 OK", [("Content-Type", "application/json")])
            return [body.encode("utf-8")]
        if path == "/partial":
            r = render_partial()
            if not r.ok:
                start_response("500 Internal Server Error", [("Content-Type", "text/plain")])
                return [("render failed: %s" % r.error).encode("utf-8")]
            start_response("200 OK", [("Content-Type", "text/html; charset=utf-8")])
            return [r.output.encode("utf-8")]
        if path == "/concurrency":
            body = json.dumps(render_concurrency())
            start_response("200 OK", [("Content-Type", "application/json")])
            return [body.encode("utf-8")]
        if path == "/error":
            r = render_error()
            if r.ok:
                start_response("500 Internal Server Error", [("Content-Type", "text/plain")])
                return [b"unexpected: render succeeded"]
            start_response("500 Internal Server Error", [("Content-Type", "text/plain")])
            return [("render failed: %s" % r.error).encode("utf-8")]
        if path == "/malformed":
            r = render_malformed()
            if r.ok:
                start_response("500 Internal Server Error", [("Content-Type", "text/plain")])
                return [b"unexpected: render succeeded"]
            start_response("500 Internal Server Error", [("Content-Type", "text/plain")])
            return [("render failed: %s" % r.error).encode("utf-8")]
        start_response("404 Not Found", [("Content-Type", "text/plain")])
        return [b"not found"]
    except Exception as ex:  # pragma: no cover
        start_response("500 Internal Server Error", [("Content-Type", "text/plain")])
        return [("exception: %s" % ex).encode("utf-8")]


if __name__ == "__main__":
    server = make_server("127.0.0.1", PORT, app)
    print("Nift Python dogfood listening on http://127.0.0.1:%d" % PORT)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        print("shutting down; disposing engine")
        engine.close()
