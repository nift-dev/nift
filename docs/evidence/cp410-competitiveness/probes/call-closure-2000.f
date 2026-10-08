offset := 1
f := x => x + offset
s := 0

i := 0
while(i < 2000) { s += f(i); i += 1 }
print(s)

