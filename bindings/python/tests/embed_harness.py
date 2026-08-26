# Nift Embed shared-corpus adapter (seventh): JSON request on stdin -> JSON
# result on stdout, implementing the same neutral protocol as cpp-embed,
# rust-embed, c-abi, go-embed, cs-embed and js-embed. Loader/env seams go
# through the CPython extension (GIL re-acquired in callbacks), proving
# worker-thread callback routing from a Python consumer.
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from nift import Context, Engine  # noqa: E402


def emit(doc):
    sys.stdout.write(json.dumps(doc) + "\n")


def relative_keys(root, keys):
    # Separator normalization: the engine reports loader keys with forward
    # slashes (generic_string) on every platform.
    def norm(s):
        return s.replace("\\", "/")

    prefix = norm(root).rstrip("/") + "/"
    seen = set()
    for k in keys:
        kn = norm(k)
        seen.add(kn[len(prefix):] if kn.startswith(prefix) else kn)
    return sorted(seen)


def main():
    req = json.load(sys.stdin)
    root = req["root"]
    mode = req.get("mode", "composed")
    seam = req.get("seam", "-")

    engine = Engine.open(root) if mode == "page" else Engine.new()
    if mode != "page":
        engine.set_root(root)
    context = Context()

    try:
        for name, value in (req.get("bindings") or {}).items():
            try:
                if str(value).startswith("json:"):
                    engine.set_json(name, str(value)[5:])
                else:
                    engine.set_string(name, str(value))
            except RuntimeError:
                emit({"ok": False, "error": f"invalid binding name: {name}"})
                return 0
        for name, value in (req.get("context_bindings") or {}).items():
            try:
                if str(value).startswith("json:"):
                    context.set_json(name, str(value)[5:])
                else:
                    context.set_string(name, str(value))
            except RuntimeError:
                emit({"ok": False, "error": f"invalid binding name: {name}"})
                return 0

        loader_keys = []

        def loader(path):
            loader_keys.append(path)
            if path.endswith("/templates/template.html"):
                return "<main>@content</main>\n"
            if path.endswith("/content/blog.html"):
                return "<p>LOADER-CONTENT</p>\n"
            if path.endswith("/content/post.html"):
                return '@input("part.html")\n'
            if path.endswith("/content/part.html"):
                return "<p>LOADER-PART</p>\n"
            return None

        if seam == "loader":
            engine.set_loader(loader)
        elif seam == "loader-error":
            def loader_error(path):
                raise RuntimeError("host exploded")
            engine.set_loader(loader_error)
        elif seam == "env":
            def env(name):
                return {"NIFT_ENV_A": "alpha", "NIFT_ENV_B": "beta"}.get(name)
            engine.set_environment_provider(env)
        elif seam == "env-error":
            def env_error(name):
                raise RuntimeError("host exploded")
            engine.set_environment_provider(env_error)

        if req.get("page_name"):
            context.set_page_name(req["page_name"])
        if req.get("current_output"):
            context.set_current_output(req["current_output"])

        if mode == "page":
            result = engine.render_page(req["page_name"], context)
        elif mode == "partial":
            result = engine.render_partial(req.get("page") or "", context)
        else:
            page = {"path": req["page_path"]} if req.get("page_path") else {"text": req.get("page") or ""}
            tpl = {"path": req["template_path"]} if req.get("template_path") else {"text": req.get("template") or ""}
            result = engine.render(page, tpl, context)

        if not result.ok:
            emit({"ok": False, "error": result.error or ""})
            return 0

        doc = {
            "ok": True,
            "output": result.output,
            "dependencies": result.dependencies,
            "requirements": result.requirements,
            "pagination": [{"page": p["page"], "output": p["output"]} for p in result.pagination],
        }
        if seam == "loader":
            doc["loaderKeys"] = relative_keys(root, loader_keys)
        emit(doc)
        return 0
    finally:
        context.close()
        engine.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as ex:  # pragma: no cover
        emit({"ok": False, "error": f"adapter exception: {ex}"})
        sys.exit(0)
