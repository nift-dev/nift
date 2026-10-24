try { throw error("x") } catch(e) {
  e = error("y")
  print(e.message)
}
