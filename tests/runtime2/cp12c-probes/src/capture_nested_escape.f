f:=()=>0
a:=[1,2]
for(x:a){x=7;f = ()=>{return ()=>x};x=8}
g:=f();print(g())
