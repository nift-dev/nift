a:=[3,2,1]; n:=0; fn(mk()) { n+=1; return (x,y)=>x<y }; a.sort(mk()); print(a.stringify()); print(n)
