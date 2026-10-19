struct (P) {
  x := 0
  fn(P(a)) { this.x = a }
}
p := P(7)
print(p.x)
q := P()
print(q.x)
