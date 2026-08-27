// Nift Embed shared-corpus adapter (sixth): JSON request on stdin -> JSON
// result on stdout, implementing the same neutral protocol as cpp-embed,
// rust-embed, c-abi, go-embed and cs-embed. Loader/env seams go through the
// N-API threadsafe-function bridge, proving worker-thread callback routing
// from a managed (JS) consumer.
"use strict";
const fs = require("fs");

const { Engine, Context } = require("../lib/nift.js");

function emit(doc) {
  process.stdout.write(JSON.stringify(doc) + "\n");
}

function relativeKeys(root, keys) {
  // Separator normalization: the engine reports loader keys with forward
  // slashes (generic_string) on every platform.
  const norm = (s) => s.replace(/\\/g, "/");
  const prefix = norm(root).replace(/\/+$/, "") + "/";
  const seen = new Set();
  for (const k of keys) {
    const kn = norm(k);
    seen.add(kn.startsWith(prefix) ? kn.slice(prefix.length) : kn);
  }
  return [...seen].sort();
}

async function main() {
  const input = fs.readFileSync(0, "utf8");
  const req = JSON.parse(input);
  const root = req.root;
  const mode = req.mode || "composed";
  const seam = req.seam || "-";

  const engine = mode === "page" ? Engine.open(root) : Engine.new();
  if (mode !== "page") engine.setRoot(root);
  const context = new Context();

  try {
    // Engine-default bindings; a rejected binding is a controlled setup failure.
    for (const [name, value] of Object.entries(req.bindings || {})) {
      try {
        if (String(value).startsWith("json:")) {
          engine.setJSON(name, String(value).slice(5));
        } else {
          engine.setString(name, String(value));
        }
      } catch (err) {
        emit({ ok: false, error: `invalid binding name: ${name}` });
        return 0;
      }
    }
    // Per-render Context bindings (context-over-engine precedence).
    for (const [name, value] of Object.entries(req.context_bindings || {})) {
      try {
        if (String(value).startsWith("json:")) {
          context.setJSON(name, String(value).slice(5));
        } else {
          context.setString(name, String(value));
        }
      } catch (err) {
        emit({ ok: false, error: `invalid binding name: ${name}` });
        return 0;
      }
    }

    const loaderKeys = [];
    switch (seam) {
      case "loader":
        engine.setLoader((p) => {
          loaderKeys.push(p);
          if (p.endsWith("/templates/template.html")) return "<main>@content</main>\n";
          if (p.endsWith("/content/blog.html")) return "<p>LOADER-CONTENT</p>\n";
          if (p.endsWith("/content/post.html")) return '@input("part.html")\n';
          if (p.endsWith("/content/part.html")) return "<p>LOADER-PART</p>\n";
          return null;
        });
        break;
      case "loader-error":
        engine.setLoader(() => {
          throw new Error("host exploded");
        });
        break;
      case "env":
        engine.setEnvironmentProvider((n) =>
          n === "NIFT_ENV_A" ? "alpha" : n === "NIFT_ENV_B" ? "beta" : null
        );
        break;
      case "env-error":
        engine.setEnvironmentProvider(() => {
          throw new Error("host exploded");
        });
        break;
    }

    if (req.page_name) context.setPageName(req.page_name);
    if (req.current_output) context.setCurrentOutput(req.current_output);

    let result;
    if (mode === "page") {
      result = await engine.render(req.page_name, context);
    } else if (mode === "partial") {
      result = await engine.renderText(req.page || "", context);
    } else {
      const page = req.page_path ? { path: req.page_path } : { text: req.page || "" };
      const tpl = req.template_path
        ? { path: req.template_path }
        : { text: req.template || "" };
      result = await engine.renderSources(page, tpl, context);
    }

    if (!result.ok) {
      // Errors carry only ok/error (no loaderKeys), matching the other
      // adapters: loaderKeys are part of the successful result.
      emit({ ok: false, error: result.error || "" });
      return 0;
    }

    const doc = {
      ok: true,
      output: result.output,
      dependencies: result.dependencies,
      requirements: result.requirements,
      pagination: result.pagination,
    };
    if (seam === "loader") {
      doc.loaderKeys = relativeKeys(root, loaderKeys);
    }
    emit(doc);
    return 0;
  } finally {
    context.close();
    engine.close();
  }
}

main()
  .then((code) => process.exit(code))
  .catch((err) => {
    emit({ ok: false, error: "adapter exception: " + err.message });
    process.exit(0);
  });
