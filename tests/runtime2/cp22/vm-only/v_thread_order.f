fn(w(n)) { print("t" + n); return n }
a := thread(w, 1)
b := thread(w, 2)
print(a.status())
print("main")
print(b.join())
print(a.status())
