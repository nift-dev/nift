// B4b 32 (review of ce0a9a44, N-1): control: filter() returns a new array; a store through it does not reach the receiver
xs := [" a ", " b "]
ys := xs.filter(e => true)
ys[0] = 5
for (e : xs) { print(e.trim()) }
