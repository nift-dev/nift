// B4b 32 (review of ce0a9a44, N-1): sort()'s result held in an array element, pushed through later
xs := [" a ", " b "]
holder := [xs.sort()]
t := holder[0]
t.push(5)
for (e : xs) { print(e.trim()) }
