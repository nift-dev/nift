e := "outer value"
try { throw error("x") } catch(e) {
  print(e.message)
}
print(e)
