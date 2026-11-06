// B4b 32 (review of ce0a9a44, N-1): reverse()'s result held in an object member, stored through later
xs := [" a ", " b "]
o := {"k": xs.reverse()}
t := o.k
t[0] = 5
for (e : xs) { print(e.trim()) }
