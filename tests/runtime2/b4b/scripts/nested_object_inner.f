// B4b: a nested object's member changed through an inner alias.
o := {"in": {"s": " a "}}
i := o.in
i["s"] = [1]
print("before")
print(o.in.s.trim())
