// B4b 32 (review of ce0a9a44, N-1): control: reverse() on a receiver that may be a string (refused: method:reverse)
v := [" a "]
if (false) { v = " s " }
ys := v.reverse()
ys[0] = 5
print(v[0].trim())
