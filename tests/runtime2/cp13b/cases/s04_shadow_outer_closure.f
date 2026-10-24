e := "outer value"
fs := []
try { throw error("x") } catch(e) {
  fs.push(() => e.message)
}
f := fs[0]
print(f())
print(e)
