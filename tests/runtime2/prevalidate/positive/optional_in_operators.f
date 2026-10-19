print(null?["x"] ?? 7)
print(true ? null?["x"] : 1)
o := {"a": 1}
print(o?["a"] + 1)
print(o?["a"] == 1 ? "y" : "n")
l := {"k": null?["a"]}
print(l.stringify())
m := [null?["q"], 1]
print(m.size())
print("v=$[o?.a]")
