fn(f()) { return 1 }
s := 0

i := 0
while(i < 8000) { s += f(); i += 1 }
print(s)

