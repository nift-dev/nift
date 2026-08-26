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
raw_best = float("inf")
req_best = float("inf")
for _ in range(rounds):
    start = time.perf_counter()
    for _ in range(n):  # raw: no request Context, engine-default binding
        r = e.render(page, tpl)
        if not r.ok:
            raise SystemExit(r.error)
    raw = (time.perf_counter() - start) * 1e9 / n
    if raw < raw_best:
        raw_best = raw
    start = time.perf_counter()
    for _ in range(1000):  # request-loop: fresh Context per request
        c = Context()
        c.set_string("who", "w")
        r = e.render(page, tpl, c)
        if not r.ok:
            raise SystemExit(r.error)
        c.close()
    req = (time.perf_counter() - start) * 1000
    if req < req_best:
        req_best = req
e.close()
print(f"py raw={int(raw_best)} ns/render request-loop={int(req_best)} ms/1000 rounds={rounds}")
