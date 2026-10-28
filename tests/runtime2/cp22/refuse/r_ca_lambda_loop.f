work := async (n) => { i := 0; while (i < n) { i += 1 }; return i }
print(await work(100))
