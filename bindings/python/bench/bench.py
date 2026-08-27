# CP18 part B: Python binding raw render + repeated/server render workload.
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from nift import Context, Engine

e = Engine.new()
e.set_root("/")
e.set_string("site", "nift")
page = "<p>$[site]</p>"
tpl = "<main>@content</main>"
n = 50000
rounds = 3
r = e.render_sources(page, tpl)  # warm-up (unreported)
if not r.ok:
    raise SystemExit(r.error)
raw_samples = []
req_samples = []
for _ in range(rounds):
    start = time.perf_counter()
    for _ in range(n):  # raw: no request Context, engine-default binding
        r = e.render_sources(page, tpl)
        if not r.ok:
            raise SystemExit(r.error)
    raw_samples.append((time.perf_counter() - start) * 1e9 / n)
    start = time.perf_counter()
    for _ in range(1000):  # request-loop: fresh Context per request
        c = Context()
        c.set_string("who", "w")
        r = e.render_sources(page, tpl, c)
        if not r.ok:
            raise SystemExit(r.error)
        c.close()
    req_samples.append((time.perf_counter() - start) * 1000)
raw_samples.sort()
req_samples.sort()
e.close()
print(f"py raw={int(raw_samples[rounds // 2])} ns/render "
      f"request-loop={int(req_samples[rounds // 2])} ms/1000 rounds={rounds}")
