// B4b 32 (review of ce0a9a44, N-1): control: validate returns its value argument (an alias, modelled before)
schema := {"type": "array"}
xs := [" a ", " b "]
ys := validate(schema, xs)
ys[0] = 5
for (e : xs) { print(e.trim()) }
