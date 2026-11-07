// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): APPROVED 2026-11-07 22:13 (owner D1.2), a difference, not legacy parity: legacy's prepared loop evaluator reads the receiver late (push: [7,4], [1]) or loses the mutation (insert: [7], [1]); the VM uses the canonical order in loops too.
b := [1]
keep := b
g := () => { b = [7]; return 4 }
once := 0
while (once < 1) {
  once += 1
  b.push(g())
}
print(b.stringify())
print(keep.stringify())
c := [1]
kc := c
h := () => { c = [7]; return 4 }
for (z : [0]) {
  c.insert(0, h())
}
print(c.stringify())
print(kc.stringify())
