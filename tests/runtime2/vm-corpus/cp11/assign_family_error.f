// Checkpoint 1: a failed nested store reports legacy's wording at the statement.
g := [[1], [2]]
g[1][0] = 5
print(g.stringify())
g[2][0] = 1
print("unreached")
