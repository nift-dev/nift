f:=()=>0
a:=[1,[2]]
for(x:a){a[0]=9;f = ()=>x;break}
print(f()==9)
