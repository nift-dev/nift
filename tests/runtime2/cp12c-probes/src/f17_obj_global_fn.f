o := {"a":1,"b":2,"c":3}
fn(drop()) { o = {"a":1} }
for ((k,v) : o) { drop(); print(k); print(v) }
print("done")
