fs := []
i := 0
while (i < 3) {
  try { throw error("e" + i.to_string()) } catch(e) {
    fs.push(() => e.message)
  }
  i += 1
}
f0 := fs[0]
f2 := fs[2]
print(f0() + f2())
