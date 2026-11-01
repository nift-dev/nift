// D-NEXT-refsem review round 1: an object as a map key is refused under its own reason.
print("start")
m := map()
o := {"a": 1}
m.set(o, 1)
print(m.size())
