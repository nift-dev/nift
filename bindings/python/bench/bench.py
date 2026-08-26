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
start = time.perf_counter()
for _ in range(n):
    r = e.render(page, tpl)
    if not r.ok:
        raise SystemExit(r.error)
raw = (time.perf_counter() - start) * 1e9 / n
start = time.perf_counter()
for _ in range(1000):
    c = Context()
    c.set_string("who", "w")
    r = e.render(page, tpl, c)
    if not r.ok:
        raise SystemExit(r.error)
    c.close()
server = (time.perf_counter() - start) * 1000
e.close()
print(f"py raw={int(raw)} ns/render server={int(server)} ms/1000")
