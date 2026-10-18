print("before")
try { rm("dir") } catch(e) { print("caught " + e.code) }
print("after")
