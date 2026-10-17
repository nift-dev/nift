struct(pt) {
 x := 0
 y := 0
 fn(pt(a, b)) { x = a; y = b }
}
p := pt(3, 4)
print(p.x)
print(p.y)
struct(bare) { z := 1 }
b := bare()
print(b.z)
