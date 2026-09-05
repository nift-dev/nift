// Nift Node binding focused tests. Plain Node assertion runner (no framework
// dependency); exits non-zero on failure.
//
// Covers: lifetime, GC pressure, callbacks surviving GC, exception containment,
// host Error(diagnostic), loader/environment NotFound, concurrent renders,
// pagination callbacks (which fire from C++ worker threads), shutdown while
// callbacks are quiescent, invalid/disposed object use, and the shared
// semantic surface (bindings, precedence, values, deps/reqs).
"use strict";
const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");

const { Engine, Context } = require("../lib/nift.js");

let passed = 0;
let failed = 0;

async function test(name, fn) {
  try {
    await fn();
    passed++;
    console.log("PASS " + name);
  } catch (err) {
    failed++;
    console.log("FAIL " + name + ": " + err.message);
  }
}

function makeProject() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "nift-node-test-"));
  const mkdir = (p) => fs.mkdirSync(path.join(root, p), { recursive: true });
  mkdir(".nift");
  mkdir("content");
  mkdir("templates");
  mkdir("public");
  fs.writeFileSync(
    path.join(root, ".nift/config.json"),
    '{"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","incremental-mode":"modified"}}'
  );
  fs.writeFileSync(
    path.join(root, ".nift/tracked.json"),
    '{"tracked":[{"name":"home","title":"Home","template":"templates/template.html"},{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]}'
  );
  fs.writeFileSync(path.join(root, "content/home.html"), "<p>home</p>\n");
  fs.writeFileSync(
    path.join(root, "content/blog.html"),
    "@item{one}@item{two}@item{three}@paginate"
  );
  fs.writeFileSync(
    path.join(root, "content/blog.paginate.html"),
    "<section>page $[paginate.current]/$[paginate.total]:[$[paginate.items]]</section>"
  );
  fs.writeFileSync(
    path.join(root, "templates/template.html"),
    "<main>$[title]</main>\n@content"
  );
  fs.writeFileSync(path.join(root, "content/bad.json"), "not json\n");
  return root;
}

(async () => {
  await test("abi: values/bindings render", async () => {
    const e = Engine.new();
    e.setString("s", "hello");
    e.setInt("i", 42);
    e.setNumber("n", 1.5);
    e.setBool("b", true);
    e.setJSON("v", '{"x":1,"y":"z"}');
    const r = await e.renderSources("$[s]|$[i]|$[n]|$[b]|$[v.x]|$[v.y]", "<main>@content</main>");
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.output, "<main>hello|42|1.5|true|1|z</main>");
    e.close();
  });

  await test("context bindings + context-over-engine precedence", async () => {
    const e = Engine.new();
    e.setString("site", "engine");
    const c = new Context();
    c.setString("site", "context");
    const r = await e.renderSources("$[site]", "<main>@content</main>", c);
    assert.strictEqual(r.output, "<main>context</main>");
    c.close();
    e.close();
  });

  await test("invalid engine binding -> setup failure", async () => {
    const e = Engine.new();
    assert.throws(() => e.setString("9bad", "x"), /invalid binding name: 9bad/);
    e.close();
  });

  await test("invalid context binding -> setup failure", async () => {
    const c = new Context();
    assert.throws(() => c.setString("9bad", "x"), /invalid binding name: 9bad/);
    c.close();
  });

  await test("malformed JSON content -> error_prefix family", async () => {
    const root = makeProject();
    const e = Engine.new();
    e.setRoot(root);
    const r = await e.renderSources('@json(d, "content/bad.json")$[d.x]@content', "<main>@content</main>");
    assert.strictEqual(r.ok, false);
    assert.ok(
      r.error.startsWith("json: failed to parse content/bad.json ("),
      "unexpected diagnostic: " + r.error
    );
    e.close();
    fs.rmSync(root, { recursive: true, force: true });
  });

  await test("loader Found / NotFound / Error(diagnostic)", async () => {
    const e = Engine.new();
    e.setRoot("/");
    e.setLoader((p) => (p.endsWith("/p.html") ? "<p>PART</p>" : null));
    let r = await e.renderSources('@input("p.html")', "<main>@content</main>");
    assert.strictEqual(r.output, "<main><p>PART</p></main>");
    e.close();

    const e2 = Engine.new();
    e2.setRoot("/");
    e2.setLoader((p) => (p.endsWith("/q.html") ? "<p>Q</p>" : null));
    r = await e2.renderSources('@input("missing.html")', "<main>@content</main>");
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.error, "@input path does not exist: missing.html");
    e2.close();

    const e3 = Engine.new();
    e3.setRoot("/");
    e3.setLoader(() => {
      throw new Error("host exploded");
    });
    r = await e3.renderSources('@input("p.html")', "<main>@content</main>");
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.error, "host exploded");
    e3.close();
  });

  await test("environment Found / NotFound / Error(diagnostic)", async () => {
    const e = Engine.new();
    e.setEnvironmentProvider((n) => (n === "GREETING" ? "hi" : null));
    let r = await e.renderSources("@getenv(GREETING)", "<main>@content</main>");
    assert.strictEqual(r.output, "<main>hi</main>");
    e.close();

    const e2 = Engine.new();
    e2.setEnvironmentProvider(() => {
      throw new Error("boom");
    });
    r = await e2.renderSources("@getenv(X)", "<main>@content</main>");
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.error, "getenv: boom");
    e2.close();
  });

  await test("page/pagination render with project engine", async () => {
    const root = makeProject();
    const e = Engine.open(root);
    assert.strictEqual(e.isOpen(), true);
    const r = await e.render("blog");
    assert.strictEqual(r.ok, true);
    assert.ok(r.output.includes("page 1/3"));
    assert.strictEqual(r.pagination.length, 2);
    assert.strictEqual(r.pagination[0].page, 2);
    assert.ok(r.pagination[1].output.includes("page 3/3"));
    assert.ok(r.dependencies.includes("content/blog.html"));
    e.close();
    fs.rmSync(root, { recursive: true, force: true });
  });

  await test("partial render", async () => {
    const e = Engine.new();
    const r = await e.renderText("<p>frag</p>");
    assert.strictEqual(r.output, "<p>frag</p>");
    e.close();
  });

  await test("concurrent renders with callbacks", async () => {
    const e = Engine.new();
    e.setRoot("/");
    e.setLoader((p) => (p.endsWith("/p.html") ? "<p>PART</p>" : null));
    const results = await Promise.all(
      Array.from({ length: 64 }, (_, i) =>
        e.renderSources('@input("p.html")', "<main>$[n]</main>@content", (() => {
          const c = new Context();
          c.setInt("n", i);
          return c;
        })())
      )
    );
    results.forEach((r, i) => {
      assert.strictEqual(r.ok, true);
      assert.strictEqual(r.output, `<main>${i}</main><p>PART</p>`);
    });
    e.close();
  });

  await test("pagination callbacks fire from C++ worker threads", async () => {
    // Pages 2..N are rendered by C++ pagination worker threads. Their content
    // uses @getenv, so the environment callback is invoked from those worker
    // threads and must reach the JS event loop through the threadsafe bridge.
    const root = makeProject();
    fs.writeFileSync(
      path.join(root, "content/blog.paginate.html"),
      "<section>page $[paginate.current]/$[paginate.total]:@getenv(TAG)</section>"
    );
    const e = Engine.open(root);
    let envCalls = 0;
    e.setEnvironmentProvider((n) => {
      envCalls++;
      return n === "TAG" ? "worker" : null;
    });
    const r = await e.render("blog");
    assert.strictEqual(r.ok, true);
    assert.ok(r.pagination.length >= 1, "pagination present");
    assert.ok(envCalls > 0, "environment callback invoked during paginated render");
    assert.ok(r.pagination.some((p) => p.output.includes("worker")),
      "worker-rendered pagination page carries the env value");
    e.close();
    fs.rmSync(root, { recursive: true, force: true });
  });

  await test("callbacks survive GC", async () => {
    const e = Engine.new();
    e.setRoot("/");
    {
      // Local reference goes out of scope; the engine roots the callback.
      let loader = (p) => (p.endsWith("/p.html") ? "<p>ROOTED</p>" : null);
      e.setLoader(loader);
      loader = null;
    }
    if (global.gc) global.gc();
    await new Promise((resolve) => setTimeout(resolve, 20));
    if (global.gc) global.gc();
    const r = await e.renderSources('@input("p.html")', "<main>@content</main>");
    assert.strictEqual(r.output, "<main><p>ROOTED</p></main>");
    e.close();
  });

  await test("repeated engine create/dispose (lifetime)", async () => {
    for (let i = 0; i < 200; i++) {
      const e = Engine.new();
      e.setString("s", String(i));
      const r = await e.renderSources("$[s]", "<main>@content</main>");
      assert.strictEqual(r.output, `<main>${i}</main>`);
      e.close();
    }
  });

  await test("GC pressure: many engines/contexts without leaks", async () => {
    for (let i = 0; i < 100; i++) {
      const e = Engine.new();
      const c = new Context();
      c.setString("s", "x");
      e.setString("s", "engine");
      const r = await e.renderSources("$[s]", "<main>@content</main>", c);
      assert.strictEqual(r.ok, true);
      c.close();
      e.close();
    }
    if (global.gc) global.gc();
  });

  await test("disposed object use is rejected", async () => {
    const e = Engine.new();
    e.close();
    assert.throws(() => e.setString("s", "x"), /disposed/);
    const c = new Context();
    c.close();
    assert.throws(() => c.setString("s", "x"), /disposed/);
  });

  await test("shutdown while callbacks are quiescent", async () => {
    // Dispose after renders complete; the tsfn shuts down cleanly.
    const e = Engine.new();
    e.setRoot("/");
    e.setLoader((p) => (p.endsWith("/p.html") ? "<p>PART</p>" : null));
    const r = await e.renderSources('@input("p.html")', "<main>@content</main>");
    assert.strictEqual(r.ok, true);
    e.close();
    await new Promise((resolve) => setImmediate(resolve));
  });

  await test("exception containment: throwing in loader", async () => {
    const e = Engine.new();
    e.setRoot("/");
    e.setLoader(() => {
      throw new Error("contained");
    });
    const r = await e.renderSources('@input("x.html")', "<main>@content</main>");
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.error, "contained");
    e.close();
  });

  await test("close engine during in-flight render: safe + deterministic", async () => {
    const e = Engine.new();
    e.setRoot("/");
    e.setLoader((p) => (p.endsWith("/p.html") ? "<p>PART</p>" : null));
    const p = e.renderSources('@input("p.html")', "<main>@content</main>");
    e.close(); // must NOT free the native engine while the render is in flight
    const r = await p;
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.output, "<main><p>PART</p></main>");
    assert.throws(() => e.renderSources("x", "y"), /disposed/);
  });

  await test("close context during in-flight render: safe + deterministic", async () => {
    const e = Engine.new();
    const c = new Context();
    c.setString("s", "ctx");
    const p = e.renderSources("$[s]", "<main>@content</main>", c);
    c.close(); // must NOT free the native context while the render is in flight
    const r = await p;
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.output, "<main>ctx</main>");
    assert.throws(() => c.setString("x", "y"), /disposed/);
    e.close();
  });

  await test("close engine during render with loader/env callbacks (tsfns survive)", async () => {
    const e = Engine.new();
    e.setRoot("/");
    e.setLoader((p) => {
      if (p.endsWith("/p.html")) return "<p>PART</p>";
      if (p.endsWith("/e.html")) return "@getenv(GREETING)";
      return null;
    });
    e.setEnvironmentProvider((n) => (n === "GREETING" ? "hi" : null));
    const p = e.renderSources('@input("e.html")', "<main>@content</main>");
    e.close(); // in-flight render may still need the loader/env TSFNs
    const r = await p;
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.output, "<main>hi</main>");
  });

  await test("close engine + contexts during concurrent renders", async () => {
    const e = Engine.new();
    e.setRoot("/");
    e.setLoader((p) => (p.endsWith("/p.html") ? "<p>P</p>" : null));
    const promises = [];
    for (let i = 0; i < 24; i++) {
      const c = new Context();
      c.setInt("n", i);
      promises.push(
        e.renderSources('@input("p.html")', "<main>$[n]</main>@content", c).then((r) => r.ok)
      );
      c.close(); // close while its render is outstanding
    }
    e.close(); // close while several renders are outstanding
    const results = await Promise.all(promises);
    assert.strictEqual(results.length, 24);
    assert.ok(results.every((ok) => ok === true), "all concurrent renders settled ok");
    await new Promise((resolve) => setImmediate(resolve));
  });

  await test("repeated/idempotent close", async () => {
    const e = Engine.new();
    const p = e.renderSources("x", "<main>@content</main>");
    e.close();
    e.close();
    e.close();
    await p;
    const c = new Context();
    c.close();
    c.close();
  });

  await test("GC pressure after close-during-render", async () => {
    for (let i = 0; i < 40; i++) {
      const e = Engine.new();
      e.setRoot("/");
      e.setLoader((p) => (p.endsWith("/p.html") ? "<p>P</p>" : null));
      const c = new Context();
      c.setInt("n", i);
      const p = e.renderSources('@input("p.html")', "<main>$[n]</main>@content", c);
      e.close();
      c.close();
      const r = await p;
      assert.strictEqual(r.ok, true);
    }
    if (global.gc) global.gc();
  });

  await test("long-lived engine across many renders", async () => {
    const e = Engine.new();
    e.setString("site", "persist");
    for (let i = 0; i < 500; i++) {
      const r = await e.renderSources("$[site]", "<main>@content</main>");
      assert.strictEqual(r.output, "<main>persist</main>");
    }
    e.close();
  });

  await test("CP19 render API: render/renderPath/renderText", async () => {
    const root = makeProject();
    const e = Engine.open(root);
    assert(e.isOpen(), "project opens");
    const byName = await e.render("home");
    assert(byName.ok && byName.output === "<main>Home</main>\n<p>home</p>",
      `render(name): ${JSON.stringify(byName)}`);
    const unknown = await e.render("no-such-page");
    assert(!unknown.ok && /unknown/i.test(unknown.error || ""),
      `unknown tracked name must be a controlled error: ${JSON.stringify(unknown)}`);

    const pagePath = path.join(root, "content/home.html");
    const viaPath = await e.renderPath(pagePath);
    assert(viaPath.ok && viaPath.output === "<p>home</p>\n",
      `renderPath(existing): ${JSON.stringify(viaPath)}`);
    const missingPath = await e.renderPath(path.join(root, "nope.html"));
    assert(!missingPath.ok, "renderPath(missing) must be a controlled error");
    assert(missingPath.output !== "<p>home</p>\n", "missing path must not be re-read as literal text");

    const viaText = await e.renderText("<p>literal</p>");
    assert(viaText.ok && viaText.output === "<p>literal</p>",
      `renderText: ${JSON.stringify(viaText)}`);
    const textNamingAFile = await e.renderText(pagePath);
    assert(textNamingAFile.ok && textNamingAFile.output === pagePath,
      "renderText must never resolve its argument as a file path");

    const noCtx = await e.renderText("$[x]");
    const c = Context.new();
    c.setString("x", "value");
    const withCtx = await e.renderText("$[x]", c);
    c.close();
    assert(withCtx.ok && withCtx.output === "value", `renderText(text, ctx): ${JSON.stringify(withCtx)}`);
    assert(noCtx.ok && noCtx.output === "$[x]", "no-context render must not reuse request state");

    const tplPath = path.join(root, "templates/template.html");
    const pp = await e.renderSources({ path: pagePath }, { path: tplPath });
    assert(pp.ok && pp.output === "<main></main>\n<p>home</p>", `path/path: ${JSON.stringify(pp)}`);
    const tt = await e.renderSources({ text: "<p>hi</p>" }, { text: "<main>@content</main>" });
    assert(tt.ok && tt.output === "<main><p>hi</p></main>", `text/text: ${JSON.stringify(tt)}`);
    const mixed = await e.renderSources({ text: "<p>mixed</p>" }, { path: tplPath });
    assert(mixed.ok && mixed.output === "<main></main>\n<p>mixed</p>", `text/path: ${JSON.stringify(mixed)}`);
    e.close();
    fs.rmSync(root, { recursive: true, force: true });
  });

  console.log(`\nNode binding tests: ${passed} passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
})();
