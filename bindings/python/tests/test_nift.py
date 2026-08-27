"""Nift Python binding focused tests (unittest, no external deps).

Covers the shared semantic surface plus lifetime safety: close() during
in-flight renders (on another thread) defers native destruction; callbacks from
C++ pagination workers reach Python through the GIL; GC pressure; disposed-use
rejection; exception containment.
"""
import gc
import os
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from nift import Context, Engine  # noqa: E402


def make_project():
    root = tempfile.mkdtemp(prefix="nift-py-test-")
    os.makedirs(os.path.join(root, ".nift"))
    os.makedirs(os.path.join(root, "content"))
    os.makedirs(os.path.join(root, "templates"))
    os.makedirs(os.path.join(root, "public"))
    with open(os.path.join(root, ".nift/config.json"), "w") as f:
        f.write('{"config":{"content-dir":"content/","output-dir":"public/",'
                '"default-template":"templates/template.html","incremental-mode":"modified"}}')
    with open(os.path.join(root, ".nift/tracked.json"), "w") as f:
        f.write('{"tracked":[{"name":"home","title":"Home","template":"templates/template.html"},'
                '{"name":"blog","title":"Blog","template":"templates/template.html",'
                '"paginate":{"items-per-page":1}}]}')
    with open(os.path.join(root, "content/home.html"), "w") as f:
        f.write("<p>home</p>\n")
    with open(os.path.join(root, "content/blog.html"), "w") as f:
        f.write("@item{one}@item{two}@item{three}@paginate")
    with open(os.path.join(root, "content/blog.paginate.html"), "w") as f:
        f.write("<section>page $[paginate.current]/$[paginate.total]:[$[paginate.items]]</section>")
    with open(os.path.join(root, "templates/template.html"), "w") as f:
        f.write("<main>$[title]</main>\n@content")
    with open(os.path.join(root, "content/bad.json"), "w") as f:
        f.write("not json\n")
    return root


class TestBindings(unittest.TestCase):
    def test_values_render(self):
        e = Engine.new()
        e.set_string("s", "hello")
        e.set_int("i", 42)
        e.set_number("n", 1.5)
        e.set_bool("b", True)
        e.set_json("v", '{"x":1,"y":"z"}')
        r = e.render_sources("$[s]|$[i]|$[n]|$[b]|$[v.x]|$[v.y]", "<main>@content</main>")
        self.assertTrue(r.ok)
        self.assertEqual(r.output, "<main>hello|42|1.5|true|1|z</main>")
        e.close()

    def test_context_over_engine(self):
        e = Engine.new()
        e.set_string("site", "engine")
        c = Context()
        c.set_string("site", "context")
        r = e.render_sources("$[site]", "<main>@content</main>", c)
        self.assertEqual(r.output, "<main>context</main>")
        c.close()
        e.close()

    def test_invalid_bindings(self):
        e = Engine.new()
        with self.assertRaises(RuntimeError) as cm:
            e.set_string("9bad", "x")
        self.assertEqual(str(cm.exception), "invalid binding name: 9bad")
        c = Context()
        with self.assertRaises(RuntimeError):
            c.set_string("9bad", "x")
        c.close()
        e.close()

    def test_malformed_json_family(self):
        root = make_project()
        e = Engine.new()
        e.set_root(root)
        r = e.render_sources('@json("content/bad.json", d)$[d.x]@content', "<main>@content</main>")
        self.assertFalse(r.ok)
        self.assertTrue(
            r.error.startswith("json: failed to parse content/bad.json ("),
            r.error,
        )
        e.close()
        import shutil
        shutil.rmtree(root, ignore_errors=True)

    def test_loader_found_notfound_error(self):
        e = Engine.new()
        e.set_root("/")
        e.set_loader(lambda p: "<p>PART</p>" if p.endswith("/p.html") else None)
        r = e.render_sources('@input("p.html")', "<main>@content</main>")
        self.assertEqual(r.output, "<main><p>PART</p></main>")
        e.close()

        e2 = Engine.new()
        e2.set_root("/")
        e2.set_loader(lambda p: "<p>Q</p>" if p.endswith("/q.html") else None)
        r = e2.render_sources('@input("missing.html")', "<main>@content</main>")
        self.assertFalse(r.ok)
        self.assertEqual(r.error, "@input path does not exist: missing.html")
        e2.close()

        e3 = Engine.new()
        e3.set_root("/")

        def boom(p):
            raise RuntimeError("host exploded")

        e3.set_loader(boom)
        r = e3.render_sources('@input("p.html")', "<main>@content</main>")
        self.assertFalse(r.ok)
        self.assertEqual(r.error, "host exploded")
        e3.close()

    def test_environment_found_notfound_error(self):
        e = Engine.new()
        e.set_environment_provider(lambda n: "hi" if n == "GREETING" else None)
        r = e.render_sources("@getenv(GREETING)", "<main>@content</main>")
        self.assertEqual(r.output, "<main>hi</main>")
        e.close()

        e2 = Engine.new()

        def boom(n):
            raise RuntimeError("boom")

        e2.set_environment_provider(boom)
        r = e2.render_sources("@getenv(X)", "<main>@content</main>")
        self.assertFalse(r.ok)
        self.assertEqual(r.error, "getenv: boom")
        e2.close()

    def test_page_pagination(self):
        root = make_project()
        e = Engine.open(root)
        self.assertTrue(e.is_open())
        r = e.render("blog")
        self.assertTrue(r.ok)
        self.assertIn("page 1/3", r.output)
        self.assertEqual(len(r.pagination), 2)
        self.assertEqual(r.pagination[0]["page"], 2)
        self.assertIn("page 3/3", r.pagination[1]["output"])
        self.assertIn("content/blog.html", r.dependencies)
        e.close()
        import shutil
        shutil.rmtree(root, ignore_errors=True)

    def test_partial(self):
        e = Engine.new()
        r = e.render_text("<p>frag</p>")
        self.assertEqual(r.output, "<p>frag</p>")
        e.close()

    def test_source_path(self):
        root = make_project()
        e = Engine.new()
        e.set_root(root)
        r = e.render_sources({"path": "content/home.html"}, {"path": "templates/template.html"})
        self.assertTrue(r.ok)
        self.assertIn("<p>home</p>", r.output)
        e.close()
        import shutil
        shutil.rmtree(root, ignore_errors=True)


class TestConcurrency(unittest.TestCase):
    def test_concurrent_renders_with_callbacks(self):
        e = Engine.new()
        e.set_root("/")
        e.set_loader(lambda p: "<p>PART</p>" if p.endswith("/p.html") else None)
        results = {}
        errors = []

        def worker(i):
            try:
                c = Context()
                c.set_int("n", i)
                r = e.render_sources('@input("p.html")', "<main>$[n]</main>@content", c)
                c.close()
                results[i] = r
            except Exception as ex:  # pragma: no cover
                errors.append(ex)

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(64)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(errors, [])
        self.assertEqual(len(results), 64)
        for i, r in results.items():
            self.assertTrue(r.ok)
            self.assertEqual(r.output, f"<main>{i}</main><p>PART</p>")
        e.close()

    def test_pagination_callbacks_from_worker_threads(self):
        root = make_project()
        with open(os.path.join(root, "content/blog.paginate.html"), "w") as f:
            f.write("<section>page $[paginate.current]/$[paginate.total]:@getenv(TAG)</section>")
        e = Engine.open(root)
        calls = []

        def env(name):
            calls.append(name)
            return "worker" if name == "TAG" else None

        e.set_environment_provider(env)
        r = e.render("blog")
        self.assertTrue(r.ok)
        self.assertGreaterEqual(len(r.pagination), 1)
        self.assertGreater(len(calls), 0)
        self.assertTrue(any("worker" in p["output"] for p in r.pagination))
        e.close()
        import shutil
        shutil.rmtree(root, ignore_errors=True)


class TestLifetime(unittest.TestCase):
    def test_close_engine_during_render_on_other_thread(self):
        # Deterministic rendezvous: the loader callback fires only after the
        # render has entered native execution (render_count incremented), so
        # close() provably happens during an in-flight native render.
        e = Engine.new()
        e.set_root("/")
        entered = threading.Event()
        release = threading.Event()

        def loader(p):
            entered.set()
            release.wait(10)
            return "<p>PART</p>" if p.endswith("/p.html") else None

        e.set_loader(loader)
        result = {}

        def worker():
            result["r"] = e.render_sources('@input("p.html")', "<main>@content</main>")

        t = threading.Thread(target=worker)
        t.start()
        self.assertTrue(entered.wait(10), "render never entered native execution")
        e.close()  # provably during an in-flight native render
        release.set()
        t.join()
        r = result["r"]
        self.assertTrue(r.ok)
        self.assertEqual(r.output, "<main><p>PART</p></main>")
        with self.assertRaises(RuntimeError):
            e.render_sources("x", "y")

    def test_close_context_during_render_on_other_thread(self):
        e = Engine.new()
        e.set_root("/")
        c = Context()
        c.set_string("s", "ctx")
        entered = threading.Event()
        release = threading.Event()

        def loader(p):
            entered.set()
            release.wait(10)
            return "<b>$[s]</b>" if p.endswith("/p.html") else None

        e.set_loader(loader)
        result = {}

        def worker():
            result["r"] = e.render_sources('@input("p.html")', "<main>@content</main>", c)

        t = threading.Thread(target=worker)
        t.start()
        self.assertTrue(entered.wait(10), "render never entered native execution")
        c.close()  # provably during an in-flight native render
        release.set()
        t.join()
        r = result["r"]
        self.assertTrue(r.ok)
        self.assertEqual(r.output, "<main><b>ctx</b></main>")
        with self.assertRaises(RuntimeError):
            c.set_string("x", "y")
        e.close()

    def test_close_engine_while_loader_and_env_required(self):
        # The in-flight render still needs the loader AND environment callback
        # infrastructure after close(); the env callback fires after close()
        # because the loaded content references @getenv.
        e = Engine.new()
        e.set_root("/")
        entered = threading.Event()
        release = threading.Event()
        calls = []

        def loader(p):
            calls.append("loader")
            entered.set()
            release.wait(10)
            return "@getenv(TAG)" if p.endswith("/e.html") else None

        def env(n):
            calls.append("env")
            return "worker" if n == "TAG" else None

        e.set_loader(loader)
        e.set_environment_provider(env)
        result = {}

        def worker():
            result["r"] = e.render_sources('@input("e.html")', "<main>@content</main>")

        t = threading.Thread(target=worker)
        t.start()
        self.assertTrue(entered.wait(10), "render never entered native execution")
        e.close()  # loader/env callbacks still required by the in-flight render
        release.set()
        t.join()
        r = result["r"]
        self.assertTrue(r.ok)
        self.assertIn("worker", r.output)
        self.assertIn("loader", calls)
        self.assertIn("env", calls)

    def test_close_engine_during_concurrent_renders(self):
        # Explicit latch: every render signals (via its loader callback) that
        # it is in native execution before the main thread closes the engine.
        e = Engine.new()
        e.set_root("/")
        n = 24
        lock = threading.Lock()
        entered_count = [0]
        all_entered = threading.Event()
        release = threading.Event()

        def loader(p):
            with lock:
                entered_count[0] += 1
                if entered_count[0] == n:
                    all_entered.set()
            release.wait(10)
            return "<p>P</p>" if p.endswith("/p.html") else None

        e.set_loader(loader)
        results = []
        errors = []

        def worker(i):
            try:
                c = Context()
                c.set_int("n", i)
                r = e.render_sources('@input("p.html")', "<main>$[n]</main>@content", c)
                c.close()
                results.append(r.ok)
            except Exception as ex:  # pragma: no cover
                errors.append(ex)

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(n)]
        for t in threads:
            t.start()
        self.assertTrue(all_entered.wait(10), "not all renders entered native execution")
        e.close()  # provably during N in-flight renders
        release.set()
        for t in threads:
            t.join()
        self.assertEqual(errors, [])
        self.assertEqual(len(results), n)
        self.assertTrue(all(results))

    def test_repeated_close(self):
        e = Engine.new()
        c = Context()
        e.close()
        e.close()
        c.close()
        c.close()

    def test_gc_pressure_after_close_during_render(self):
        # Deterministic: each iteration uses the loader callback as a
        # rendezvous so close() + GC pressure provably happen while a native
        # render is retained (in-flight), then the render is released.
        for i in range(20):
            e = Engine.new()
            e.set_root("/")
            entered = threading.Event()
            release = threading.Event()

            def loader(p):
                entered.set()
                release.wait(10)
                return "<p>P</p>" if p.endswith("/p.html") else None

            e.set_loader(loader)
            c = Context()
            c.set_int("n", i)
            result = {}

            def worker():
                result["r"] = e.render_sources('@input("p.html")', "<main>$[n]</main>@content", c)

            t = threading.Thread(target=worker)
            t.start()
            self.assertTrue(entered.wait(10), "render never entered native execution")
            e.close()
            c.close()
            gc.collect()  # GC pressure while the native render is retained
            release.set()
            t.join()
            self.assertTrue(result["r"].ok)
            self.assertIn(f"<main>{i}</main>", result["r"].output)
        gc.collect()

    def test_disposed_use_rejected(self):
        e = Engine.new()
        e.close()
        with self.assertRaises(RuntimeError):
            e.set_string("s", "x")
        c = Context()
        c.close()
        with self.assertRaises(RuntimeError):
            c.set_string("s", "x")


class TestLifetimeLong(unittest.TestCase):
    def test_repeated_create_dispose(self):
        for i in range(200):
            e = Engine.new()
            e.set_string("s", str(i))
            r = e.render_sources("$[s]", "<main>@content</main>")
            self.assertEqual(r.output, f"<main>{i}</main>")
            e.close()

    def test_long_lived_engine(self):
        e = Engine.new()
        e.set_string("site", "persist")
        for i in range(200):
            r = e.render_sources("$[site]", "<main>@content</main>")
            self.assertEqual(r.output, "<main>persist</main>")
        e.close()

    def test_exception_containment(self):
        e = Engine.new()
        e.set_root("/")

        def boom(p):
            raise RuntimeError("contained")

        e.set_loader(boom)
        r = e.render_sources('@input("x.html")', "<main>@content</main>")
        self.assertFalse(r.ok)
        self.assertEqual(r.error, "contained")
        e.close()



    def test_cp19_render_api(self):
        root = make_project()
        e = Engine.open(root)
        self.assertTrue(e.is_open)

        r = e.render("home")
        self.assertTrue(r.ok)
        self.assertIn("<p>home</p>", r.output)

        unknown = e.render("no-such-page")
        self.assertFalse(unknown.ok)
        self.assertIn("unknown", unknown.error.lower())

        page_path = os.path.join(root, "content", "home.html")
        r = e.render_path(page_path)
        self.assertTrue(r.ok)
        self.assertEqual(r.output, "<p>home</p>\n")

        missing = e.render_path(os.path.join(root, "nope.html"))
        self.assertFalse(missing.ok)
        self.assertNotEqual(missing.output, "<p>home</p>")

        r = e.render_text("<p>literal</p>")
        self.assertTrue(r.ok)
        self.assertEqual(r.output, "<p>literal</p>")

        text_naming_file = e.render_text(page_path)
        self.assertTrue(text_naming_file.ok)
        self.assertEqual(text_naming_file.output, page_path)

        no_ctx = e.render_text("$[x]")
        c = Context()
        c.set_string("x", "value")
        with_ctx = e.render_text("$[x]", c)
        self.assertTrue(with_ctx.ok)
        self.assertEqual(with_ctx.output, "value")
        self.assertEqual(no_ctx.output, "$[x]")

        tpl_path = os.path.join(root, "templates", "template.html")
        pp = e.render_sources({"path": page_path}, {"path": tpl_path})
        self.assertTrue(pp.ok)
        self.assertIn("<p>home</p>", pp.output)
        tt = e.render_sources("<p>hi</p>", "<main>@content</main>")
        self.assertTrue(tt.ok)
        self.assertEqual(tt.output, "<main><p>hi</p></main>")
        e.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
