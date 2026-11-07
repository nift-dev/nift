// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): index reads of an array and an object: the container as evaluated (legacy 1, 1).
b := [1, 2]
fn(g()) { b = [7, 8]; return 0 }
print(b[g()])
o := {"k": 1}
fn(h()) { o = {"k": 2}; return "k" }
print(o[h()])
