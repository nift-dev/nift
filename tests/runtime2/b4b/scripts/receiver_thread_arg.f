// B4b 32 (review of ce0a9a44, N-1): control: reverse()'s result as a thread argument (copied, D7)
fn(w(a)) {
  a.push(5)
  return 0
}
xs := [" a "]
t := thread(w, xs.reverse())
t.join()
for (e : xs) { print(e.trim()) }
