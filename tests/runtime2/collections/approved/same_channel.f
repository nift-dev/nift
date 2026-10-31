print("ran")
a := channel(1)
b := a
c := channel(1)
print(same(a, b))
print(same(a, c))
