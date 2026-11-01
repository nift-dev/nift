// D-NEXT-refsem #4: a mutual reference is refused (legacy text).
m := map()
n := map()
m.set(1, n)
print("before")
n.set(1, m)
print("never")
