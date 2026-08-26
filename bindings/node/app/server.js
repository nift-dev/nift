// Nift Node HTTP dogfood: a small genuine Node HTTP server using Nift Embed.
//
// Node owns HTTP; Nift owns template rendering. A long-lived project Engine
// serves page/pagination renders, an environment provider supplies resource
// values, a loader-backed standalone render exercises the seam, and the error
// paths return 500 with the verbatim diagnostic. The engine is disposed
// deterministically on shutdown.
//
// Renders are asynchronous (they run on a native worker thread so the JS event
// loop can service host callbacks). Handlers await the result; a Context must
// stay alive until the render promise settles (close it after awaiting).
"use strict";
const http = require("http");
const path = require("path");

const { Engine, Context } = require("../lib/nift.js");

const PORT = Number(process.env.PORT || 5173);
const SITE_ROOT = path.join(__dirname, "site");

const engine = Engine.open(SITE_ROOT);
engine.setString("site", "Nift Node Dogfood");
engine.setEnvironmentProvider((name) => {
  if (name === "SITE_VERSION") return "1.0";
  if (name === "FORCE_ERROR") throw new Error("forced host error");
  return null;
});

async function renderHome() {
  const ctx = new Context();
  ctx.setString("who", "world");
  try {
    return await engine.renderPage("home", ctx);
  } finally {
    ctx.close();
  }
}

async function renderPosts() {
  return engine.renderPage("blog");
}

async function renderPartial() {
  const partialEngine = Engine.new();
  partialEngine.setRoot("/");
  partialEngine.setLoader((p) =>
    p.endsWith("/greeting.html") ? "<p>from loader</p>\n" : null
  );
  try {
    return await partialEngine.render('@input("greeting.html")', "<main>@content</main>");
  } finally {
    partialEngine.close();
  }
}

async function renderConcurrency() {
  let failures = 0;
  for (let i = 0; i < 32; i++) {
    const ctx = new Context();
    ctx.setString("who", "c" + i);
    const r = await engine.renderPage("home", ctx);
    ctx.close();
    if (!r.ok) failures++;
  }
  return { rendered: 32, failures };
}

async function renderError() {
  const ctx = new Context();
  try {
    return await engine.render("@getenv(FORCE_ERROR)", "<main>@content</main>", ctx);
  } finally {
    ctx.close();
  }
}

async function renderMalformed() {
  const ctx = new Context();
  try {
    return await engine.render('@json("content/bad.json", d)$[d.x]', "<main>@content</main>", ctx);
  } finally {
    ctx.close();
  }
}

const server = http.createServer((req, res) => {
  const url = req.url || "/";
  const send = (status, contentType, body) => {
    res.writeHead(status, { "Content-Type": contentType });
    res.end(body);
  };
  (async () => {
    try {
      if (url === "/") {
        const r = await renderHome();
        if (!r.ok) return send(500, "text/plain", `render failed: ${r.error}`);
        return send(200, "text/html", r.output);
      }
      if (url === "/posts") {
        const r = await renderPosts();
        if (!r.ok) return send(500, "text/plain", `render failed: ${r.error}`);
        return send(
          200,
          "application/json",
          JSON.stringify({
            ok: r.ok,
            output: r.output,
            pages: r.pagination,
            dependencies: r.dependencies,
          })
        );
      }
      if (url === "/partial") {
        const r = await renderPartial();
        if (!r.ok) return send(500, "text/plain", `render failed: ${r.error}`);
        return send(200, "text/html", r.output);
      }
      if (url === "/concurrency") {
        return send(200, "application/json", JSON.stringify(await renderConcurrency()));
      }
      if (url === "/error") {
        const r = await renderError();
        if (r.ok) return send(500, "text/plain", "unexpected: render succeeded");
        return send(500, "text/plain", `render failed: ${r.error}`);
      }
      if (url === "/malformed") {
        const r = await renderMalformed();
        if (r.ok) return send(500, "text/plain", "unexpected: render succeeded");
        return send(500, "text/plain", `render failed: ${r.error}`);
      }
      return send(404, "text/plain", "not found");
    } catch (err) {
      return send(500, "text/plain", "exception: " + err.message);
    }
  })();
});

function shutdown() {
  console.log("shutting down; disposing engine");
  engine.close();
  server.close(() => process.exit(0));
}

server.listen(PORT, () => {
  console.log(`Nift Node dogfood listening on http://127.0.0.1:${PORT}`);
});
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
