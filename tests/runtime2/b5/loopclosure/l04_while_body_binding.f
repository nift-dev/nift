// B5 D1 (owner 2026-11-07 06:55, decision 1): a while body's declaration is a distinct binding per iteration
fs := []
i := 0
while (i < 3) {
  j := i
  fs.push(() => j)
  i = i + 1
}
f0 := fs[0]
f2 := fs[2]
print(f0())
print(f2())
