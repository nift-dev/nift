import("./owner.f")
s := 0; i := 0
while(i < 2000) { s += wave_owner(i); i += 1 }; print(s)
; 
