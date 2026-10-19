struct (P) {
  x := 1
  y := "a"
  fn(sum(n)) { return this.x + n }
}
p := P()
print(p.x)
print(p.sum(4))
p.x = 5
print(p.sum(4))
print(p.y)
