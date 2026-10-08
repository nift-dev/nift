fn(f(x)) { return x }
a := [1,2,{"nested":[3,4]}]; i := 0; s := 0
while(i < 2000) { b := f(a); s += b.length(); i += 1 }; print(s)
; 
