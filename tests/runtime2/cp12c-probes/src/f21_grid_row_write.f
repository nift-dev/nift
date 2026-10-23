g := [[1,2],[3,4]]
i := 0
for (row : g) { g[i][0] = 99; print(row[0]); i += 1 }
print(g[0][0])
