o := {"a":1,"b":2}
fn(drop()) { o = {"a":1} }
drop()
print(o["a"])
