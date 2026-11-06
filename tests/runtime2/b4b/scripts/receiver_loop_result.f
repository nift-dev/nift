// B4b 32 (review of ce0a9a44, N-1): reverse()'s result stored through in a loop
xs := [" a ", " b "]
for (k : [1, 2]) {
  ys := xs.reverse()
  ys[0] = k
}
for (e : xs) { print(e.trim()) }
