@import("../src/csv.f")

print(csv.parse("a,b\r\nc,d").stringify())
print(csv.stringify([["a,b", "x\"y"]]))
