nest := [[1], [2]]
fn[async](inner_global()) { sleep(2); inner := nest[1]; return inner.size() }
q := inner_global()
inner2 := nest[1]
inner2.push(9)
print(await q)
print(nest[1].size())
