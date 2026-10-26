fn(thr(m)) { throw error("boom " + m, "user.thread") }
t := thread(thr, "a")
try { t.join() } catch(e) { print(e.message); print(e.code) }
try { t.join() } catch(e2) { print(e2.message) }
print(t.status())
print(t.done())
print("after")
