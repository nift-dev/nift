fn(ap(f, v)) { return f(v) }
print(ap((x) => x ? "yes" : "no", false))
print(ap(x => x ? "yes" : "no", true))
