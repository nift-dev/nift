o := {"a":1,"b":2,"c":3}
s := () => { o = {"a":1}
  return 0 }
for ((k,v) : o) { s(); print(k); print(v) }
print("done")
