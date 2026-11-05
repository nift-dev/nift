// B4b C3: a store under a computed key reaches every member.
o := {"s": " a "}
k := "s"
o[k] = 3
print("before")
print(o.s.trim())
