// D-NEXT-refsem #1: admitted; a native's callback that runs no user code and
// mutates nothing: the receiver is unchanged while it runs.
xs := [3, 1, 2]
xs.push(4)
ys := xs.map((v) => v * 2)
print(ys.stringify())
xs.push(5)
print(ys.length())
