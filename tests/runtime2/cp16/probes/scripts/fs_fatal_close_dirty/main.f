print("before")
f := file("d.txt"); f.open("w"); f.write("x")
try { f.close() } catch(e) { print("caught " + e.code) }
print("after")
