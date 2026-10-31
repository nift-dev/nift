// D-NEXT-refsem #1: an extracted nested collection is the stored one.
outer := map()
outer.set("a", map())
x := outer.get("a")
x.set(1, 1)
y := outer.get("a")
print(y.size())
print(same(x, y))
