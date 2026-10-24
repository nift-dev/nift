f:=()=>0
a:=[1,2]
for(x:a){a[0]=9;x=7;f = ()=>x;print(x)}
print(f())
