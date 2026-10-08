fn(apply(f,x)) { return f(x) }
f := x => x + 1
s := 0

i := 0
while(i < 8000) { s += apply(f,i); i += 1 }
print(s)

