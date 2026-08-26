// CP18 part B: Node binding raw render + repeated/server render workload.
const { Engine, Context } = require("../lib/nift.js");
const { performance } = require("perf_hooks");
(async () => {
  const e = Engine.new();
  e.setRoot("/");
  e.setString("site", "nift");
  const page = "<p>$[site]</p>";
  const tpl = "<main>@content</main>";
  const n = 50000;
  let start = performance.now();
  for (let i = 0; i < n; i++) {
    const r = await e.render(page, tpl);
    if (!r.ok) throw new Error(r.error);
  }
  const raw = (performance.now() - start) * 1e6 / n;
  start = performance.now();
  for (let i = 0; i < 1000; i++) {
    const c = new Context();
    c.setString("who", "w");
    const r = await e.render(page, tpl, c);
    if (!r.ok) throw new Error(r.error);
    c.close();
  }
  const server = performance.now() - start;
  e.close();
  console.log(`node raw=${Math.round(raw)} ns/render server=${Math.round(server)} ms/1000`);
})();
