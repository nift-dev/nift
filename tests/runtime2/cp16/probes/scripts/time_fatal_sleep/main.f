print("before")
try { sleep(-1) } catch(e) { print("caught " + e.code) }
print("after")
