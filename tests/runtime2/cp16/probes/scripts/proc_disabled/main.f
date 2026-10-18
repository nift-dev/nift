print("before")
c := cmd("true")
print("cmd built")
try { run("true") } catch(e) { print("caught " + e.code) }
print("after")
