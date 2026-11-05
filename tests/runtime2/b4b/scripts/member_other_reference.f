// B4b: an object member changed through a different reference.
o := {"s": " a "}
p := o
p["s"] = 5
print("before")
print(o.s.trim())
