fn(helper(o)) { return o.missing }
fn[async](f()) { return helper({"a": 1}) }
print(await f())
