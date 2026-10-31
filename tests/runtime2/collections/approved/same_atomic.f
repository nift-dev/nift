print("ran")
a := atomic<int>(1)
b := a
c := atomic<int>(1)
print(same(a, b))
print(same(a, c))
b.store(5)
print(a.load())
