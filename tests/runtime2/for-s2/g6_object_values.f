o := {"a":1,"b":2,"c":3}
for ((k,v) : o) { print(k); print(v); if (k == "a") { o = {"a":1,"b":9,"c":3} } }
