fn(f(x)) { if(x == 0) { return 0 }; return 1 + f(x-1) }
s := 0

i := 0
while(i < 8000) { s += f(3); i += 1 }
print(s)

