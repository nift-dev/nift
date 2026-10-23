o := {"a":1,"b":2,"c":3}
for ((k,v) : o) { print(k); print(v); o = {"c":30,"b":20,"a":10} }
print("done")
