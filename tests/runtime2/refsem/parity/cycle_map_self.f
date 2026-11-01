// D-NEXT-refsem #4: a map stored into itself is refused (legacy text).
m := map()
print("before")
m.set("x", m)
print("never")
