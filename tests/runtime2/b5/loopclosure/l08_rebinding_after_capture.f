// B5 D1 (owner 2026-11-07 06:55, decision 1): no source mutation: the closure shares the loop binding, so x = 7 after its creation is seen (legacy: 1; snapshot iteration plays no part)
f:=()=>0
a:=[1]
for(x:a){f = ()=>x;x=7}
print(f())
print(a[0])
