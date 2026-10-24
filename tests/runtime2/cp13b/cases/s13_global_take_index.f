g := [1, 2]
try {
  g[5] = 3
} catch(e) {
  print("caught")
}
print(g.length())
