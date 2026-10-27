arr := [1, 2]
fn[async](grow(xs)) { xs.push(9); xs.push(9); return xs.size() }
fn[async](size_of_global()) { sleep(2); return arr.size() }
fn[async](grow_global()) { arr.push(7); return arr.size() }
a := grow(arr)
b := size_of_global()
g := grow_global()
arr.push(3)
print(await a)
print(await b)
print(await g)
print(arr.size())
nest := [[1], [2]]
fn[async](grow_outer(xs)) { xs.push([5]); xs.push([6]); return xs.size() }
print(await grow_outer(nest))
print(nest.size())
fn[async](read_nested()) { sleep(2); return nest[1].size() + nest.size() }
q := read_nested()
nest.push([3])
print(await q)
print(nest.size())
