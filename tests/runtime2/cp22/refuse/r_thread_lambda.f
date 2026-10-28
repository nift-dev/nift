l := (x) => { return x + 1 }
t := thread(l, 1)
print(t.join())
