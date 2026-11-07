// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): APPROVED 2026-11-07 22:13 (owner D1.1), a difference, not legacy parity: legacy's element store reads the receiver after a value that calls a `fn` ([4,8], [1,2]) but before one that calls a lambda (e04); the VM uses one order, receiver first: [7,8], [4,2].
b := [1, 2]
keep := b
fn(g()) { b = [7, 8]; return 4 }
b[0] = g()
print(b.stringify())
print(keep.stringify())
