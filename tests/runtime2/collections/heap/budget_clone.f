a := map()
i := 0
while (i < 25000) { a.set(i, i); i += 1 }
b := a
b.set("x", 1)
print("done")
