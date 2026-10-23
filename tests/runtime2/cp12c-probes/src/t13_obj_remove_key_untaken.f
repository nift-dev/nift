o := {"a":1,"b":2,"c":3}
go := false
for ((k,v) : o) { print(k); if (go) { print(v) }; o = {"a":1} }
print("done")
