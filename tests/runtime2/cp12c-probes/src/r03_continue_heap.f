o := {"a":[1,2],"b":[3]}
n := 0
for ((k,v) : o) { if (k == "a") { continue }
  n += 1 }
print(n)
