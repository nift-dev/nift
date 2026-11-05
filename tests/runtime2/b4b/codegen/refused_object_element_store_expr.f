// P1 audit (refused before any effect: expression:assignment-target / index-target): an object element store used as a value (constant and register values).
o := {"k": 1}
v := 3
print(o["k"] = 5)
print(o["k"] = v)
