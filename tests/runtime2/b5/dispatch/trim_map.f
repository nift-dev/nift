// B5 item B: a string element replaced by a map through an alias; trim is admitted with a
// run-time kind check and gives legacy's failure for a map.
xs := [" a "]
ys := xs
print(xs[0].trim())
ys[0] = map()
print(xs[0].trim())
