fn(apply(cb)) { return cb(3) }
o := {"f": (x) => x + 1}
g := o.f
print(apply(g))
