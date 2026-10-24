fn(maker()) { x:=1; f:=() => { g:=() => {x:=2; return x}; print(x); return g() }; return f }
f:=maker(); print(f())
