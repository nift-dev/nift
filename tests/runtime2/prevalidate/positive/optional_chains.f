o := {"a": {"b": {"c": {"d": 4}}}}
print(o?.a?["b"]?.c?["d"])
print(o?.a?.b?["c"]?.d)
n := {"a": null}
print(n?.a?["b"]?.c)
print(null?["x"]?["y"]?["z"])
print(null?["x"]?.y?["z"]?.w)
a := [[1, 2]]
print(a?[0]?[1])
k := "a"
print(o?[k]?["b"]?.c?.d)
print(o?[("a")]?["b"]?["c"]?["d"])
print(null ?. x)
print(null ?["x"])
