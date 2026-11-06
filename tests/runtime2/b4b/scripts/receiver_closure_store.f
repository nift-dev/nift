// B4b 32 (review of ce0a9a44, N-1): a closure stores through sort() of a captured binding
xs := [" a ", " b "]
g := () => {
  ys := xs.sort()
  ys[0] = 5
  return 0
}
g()
for (e : xs) { print(e.trim()) }
