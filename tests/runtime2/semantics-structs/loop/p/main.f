struct (P) { x := 0
 fn(inc()) { this.x += 1 } }
p := P()
i := 0
while (i < 3) { p.inc()
i += 1 }
print(p.x)
