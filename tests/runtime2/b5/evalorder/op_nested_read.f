// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): nested reads: the inner container as evaluated before the key (legacy 2, 2).
b := [[1, 2]]
fn(g()) { b = [[7, 8]]; return 0 }
print(b[g()][1])
c := [[1, 2]]
fn(h()) { c = [[7, 8]]; return 1 }
print(c[0][h()])
