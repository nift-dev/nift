fn(work(n)) { i := 0; x := 0; while (i < n) { x += i; i += 1 }; return x }
fn(spin(h)) { polls := 0; while (!h.done()) { polls += 1 }; return polls >= 0 }
b := thread(work, 300000)
a := thread(spin, b)
print(a.join())
print(b.join())
