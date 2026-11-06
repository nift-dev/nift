// §3 Returns + B4b 32: reverse() and sort() return the receiver.
xs := [3, 1, 2]
ys := xs.sort()
ys.push(9)
print(xs.stringify())
zs := xs.reverse()
print(same(zs, xs))
