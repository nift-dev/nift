fn(tick(n)) { i := 0; s := 0; while (i < n) { sleep(1); s += i; i += 1 }; return s }
a := thread(tick, 5)
b := thread(tick, 3)
print(a.join() + b.join())
