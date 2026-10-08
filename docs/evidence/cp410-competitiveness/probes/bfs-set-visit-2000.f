s := set(); i := 0; while(i < 2000) { s.add(i); i += 1 }; i = 0; n := 0; while(i < 2000) { if(s.contains(i)) { n += 1 }; i += 1 }; print(n)
; 
