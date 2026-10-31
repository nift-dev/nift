fn(w(x)) { return x * 2 }
print("ran")
t := thread(w, 2)
u := t
v := thread(w, 2)
print(same(t, u))
print(same(t, v))
print(t.join())
print(u.join())
print(v.join())
