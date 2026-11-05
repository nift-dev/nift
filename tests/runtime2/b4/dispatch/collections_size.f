// B4: a binding that is a set or a stack: `size` and `empty` at run time.
print("before")
x := 2
c := set()
if (x > 1) { c = stack() }
print(c.size())
print(c.empty())
