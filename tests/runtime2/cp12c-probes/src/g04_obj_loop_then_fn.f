o := {"a":1,"b":2}
fn(reset()) { o = {"a":5,"b":6} }
for ((k,v) : o) { print(k) }
reset()
print(o["a"])
