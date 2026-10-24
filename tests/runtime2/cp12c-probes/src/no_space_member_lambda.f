fn(make()){x:=()=>null; o:={"x":1}; x=()=>o.x; return x}
f:=make();print(f())
