struct (P) {
  fn(m()) { return 1 }
  fn(m()) { return 2 }
}
p := P()
print(p.m())
