f:=()=>0
a:=[1]
for(x:a){a[0]=9;f = ()=>{return ()=>x}}
g:=f();print(g())
