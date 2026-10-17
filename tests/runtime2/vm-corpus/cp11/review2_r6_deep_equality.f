fn(wrap(p)) { return [p ?? 0] }
x := [1]
y := [1]
i := 0
while(i < 2000) { x = wrap(x); y = wrap(y); i += 1 }
print(x == y)
