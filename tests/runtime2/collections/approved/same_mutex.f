print("ran")
a := mutex(0)
b := a
c := mutex(0)
print(same(a, b))
print(same(a, c))
a.lock()
print(b.locked())
a.unlock()
