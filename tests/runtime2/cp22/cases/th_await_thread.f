fn(w(x)) { return x * 2 }
t := thread(w, 2)
print(t.join())
print(await t)
