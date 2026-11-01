// D-NEXT-refsem (sound inference): what an alias stores, every alias may read: the map read
// back through `a` may hold a collection, so html_escape() of it is refused (legacy sees its tag).
print("start")
a := map()
b := a
b.set(1, map())
x := a.get(1)
print(html_escape(x))
