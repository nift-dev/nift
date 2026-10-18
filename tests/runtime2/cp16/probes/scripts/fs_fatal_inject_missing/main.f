// inject of a missing file is fatal, unlike open() (io.open_failed, catchable).
print("before")
try { inject("missing.json") } catch(e) { print("caught " + e.code) }
print("after")
