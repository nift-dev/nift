inner := stack()
inner.push([1, 2])
mid := map()
mid.set("s", inner)
outer := map()
outer.set("m", mid)
dup := outer
t := dup.get("m")
u := t.get("s")
u.push([3])
t.set("s", u)
dup.set("m", t)
print(outer.stringify())
print(dup.stringify())
print(mid.stringify())
print(inner.stringify())
arr := [outer, dup]
e := arr[1]
f := e.get("m")
f.set("x", 1)
e.set("m", f)
arr[1] = e
print(arr[0].stringify())
print(arr[1].stringify())
