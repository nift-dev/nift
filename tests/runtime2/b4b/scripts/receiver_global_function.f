// B4b 32 (review of ce0a9a44, N-1): a function stores through reverse() of a global
xs := [" a ", " b "]
fn(g()) {
  ys := xs.reverse()
  ys[1] = 5
  return 0
}
g()
for (e : xs) { print(e.trim()) }
