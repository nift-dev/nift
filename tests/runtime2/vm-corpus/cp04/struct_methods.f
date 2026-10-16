struct(Point) { x := 0; y := 0; fn(sum()) { return x + y } }
p := Point(2, 3)
print(p.sum())
p.x = 10
print(p.sum())
