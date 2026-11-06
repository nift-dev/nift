// B5 (owner Q7 ruling): the sorted elements are the original references (identity kept).
a := [2]
b := [1]
xs := [a, b]
xs.sort((p, q) => p[0] < q[0])
print(xs.stringify())
fst := xs[0]
print(same(fst, b))
b.push(9)
print(xs.stringify())
