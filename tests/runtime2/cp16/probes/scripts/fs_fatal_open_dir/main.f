// A directory where a file is expected is a fatal (uncatchable) error in legacy.
print("before")
try { open("dir") } catch(e) { print("caught " + e.code) }
print("after")
