f:=()=>0
g:=()=>0
a:=[1]
for(x:a){a[0]=9;f = ()=>x;a[0]=10;g = ()=>x}
print(f());print(g())
