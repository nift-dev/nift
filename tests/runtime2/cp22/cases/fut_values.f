fn[async](f(a)) { return a }
hs := [f(1), f(2)]
hs.push(f(3))
s := 0
for(h : hs) { s += await h }
print(s)
print(await hs[0])
print(await hs[0])
print(hs.size())
print(type(hs[1]))
p := f(9)
q := p
print(!p)
if (p) { print("truthy") }
print(await q)
