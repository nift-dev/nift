o := {"a":1,"b":2}
for ((k,v) : o) { print(k); o = {"z":1} }
print("done")
