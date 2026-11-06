// B5 (owner Q7 ruling, B2 sort protection): a comparator that mutates the receiver (through an
// alias) raises a catchable error before any change; the receiver is unchanged after it.
xs := [3, 1, 2]
ys := xs
try { xs.sort((p, q) => { ys.push(0); return p < q }) } catch (e) { print("caught") }
print(xs.stringify())
zs := [3, 1, 2]
other := [7]
zs.sort((p, q) => { other.push(p); return p < q })
print(zs.stringify())
print(other.length() > 1)
