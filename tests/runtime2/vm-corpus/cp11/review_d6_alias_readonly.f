fn(f(p)) { return p.length() + p[0] }
o := {"k": [5, 6]}
a := [[7]]
print(f(o.k))
print(f(a[0]))
print(o.k.length())
