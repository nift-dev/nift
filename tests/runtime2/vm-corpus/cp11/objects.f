fn(get(obj, k)) { return obj[k] }
o := {"alpha": 1, "beta": "two", "gamma": [3, 4], "delta": {"x": 5}}
print(o.alpha)
print(o["beta"])
print(o.gamma[1])
print(o.delta.x)
print(o["delta"]["x"])
print(get(o, "alpha"))
big := {"a": 1, "b": 2, "c": 3, "d": 4, "e": 5, "f": 6, "g": 7, "h": 8, "i": 9, "j": 10}
print(big.j + big["a"])
empty := {}
print(empty == {})
