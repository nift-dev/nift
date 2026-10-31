fn(mk()) {
  m := map()
  m.set("a", 1)
  return [m, m]
}
export(mk)
