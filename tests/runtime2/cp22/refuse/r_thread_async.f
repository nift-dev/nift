fn[async](a(x)) { return x }
t := thread(a, 3)
print(type(t.join()))
