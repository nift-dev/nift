say := (n) => { print(n); return n }
o := {"push": say}
fn[async](a(io)) { sleep(100); return io.push(1) }
fn[async](b(io)) { sleep(150); return io.push(2) }
fn[async](c(io)) { return io.push(3) }
x := a(o)
vx := await x
y := b(o)
vy := await y
z := c(o)
vz := await z
print(vx + vy + vz)
