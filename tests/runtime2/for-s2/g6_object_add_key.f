o := {"a":1,"b":2}
for ((k,v) : o) { print(k); print(v); if (k == "a") { o = {"a":1,"b":2,"c":3} } }
