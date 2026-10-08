fn(f(x)) { a := x + 1; b := a * 2; return b }
s := 0

i := 0
while(i < 4000) { s += f(i); i += 1 }
print(s)

