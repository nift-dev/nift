h:=()=>0
a:=[[1]]
for(x:a){h = ()=>{return ()=>x[0]}}
g:=h()
a=[]
print(g())
