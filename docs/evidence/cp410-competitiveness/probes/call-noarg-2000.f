fn(f()) { return 1 }
s := 0

i := 0
while(i < 2000) { s += f(); i += 1 }
print(s)

