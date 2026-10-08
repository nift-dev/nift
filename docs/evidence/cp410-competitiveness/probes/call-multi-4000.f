fn(f(x,y,z)) { return x+y+z }
s := 0

i := 0
while(i < 4000) { s += f(i,1,2); i += 1 }
print(s)

