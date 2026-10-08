fn(inc(x)) { return x + 1 }
total := 0
i := 1
while(i <= 1000) { total = inc(total); i += 1 }
print(total)
