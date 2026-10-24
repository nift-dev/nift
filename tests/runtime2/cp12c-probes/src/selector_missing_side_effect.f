f:=()=>0
a:=[[1]]
for(x:a){print(x[0]);f = ()=>{x[0]=9;print("after-write");return x[0]}}
a=[]
print(f())
