fn(f(x)) { return x + 1 }
s := 0

i := 0
while(i < 4000) { s += f(i); i += 1 }
print(s)

