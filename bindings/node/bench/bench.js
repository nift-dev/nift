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
  const rounds = 3;
  await e.render(page, tpl); // warm-up (unreported)
  const rawSamples = [], reqSamples = [];
  for (let r = 0; r < rounds; r++) {
    let start = performance.now();
    for (let i = 0; i < n; i++) { // raw: no request Context, engine-default binding
      const res = await e.render(page, tpl);
      if (!res.ok) throw new Error(res.error);
    }
    rawSamples.push((performance.now() - start) * 1e6 / n);
    start = performance.now();
    for (let i = 0; i < 1000; i++) { // request-loop: fresh Context per request
      const c = new Context();
      c.setString("who", "w");
      const res = await e.render(page, tpl, c);
      if (!res.ok) throw new Error(res.error);
      c.close();
    }
    reqSamples.push(performance.now() - start);
  }
  rawSamples.sort((a, b) => a - b);
  reqSamples.sort((a, b) => a - b);
  e.close();
  console.log(`node raw=${Math.round(rawSamples[1])} ns/render request-loop=${Math.round(reqSamples[1])} ms/1000 rounds=${rounds}`);
})();
