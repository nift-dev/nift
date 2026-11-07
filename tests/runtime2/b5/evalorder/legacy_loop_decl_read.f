// APPROVED 2026-11-07 22:13 (owner D1.3), a difference, not legacy parity (review of 0d853db4 M1; combined-b3-b5.md 2.1): `r := b[g()]` in loop code. Legacy
// evaluates the key twice there (the second call reads the rebound b): 8; 17f7964c 8. The VM evaluates
// operands once and reads the receiver first: 2 (b[1] of the original), and g runs once.
b := [1, 2]
n := 0
g := () => { n += 1; b = [7, 8]; return 1 }
i := 0
while (i < 1) {
  i += 1
  x := b[g()]
  print(x)
}
print(n)
