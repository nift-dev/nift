struct (P) { x := 2
 fn(a()) { return this.b() * 10 }
 fn(b()) { return this.x } }
p := P()
print(p.a())
