a := []; i := 0; while(i < 2000) { a.push([i,i+1]); i += 1 }; i = 0; s := 0; while(i < 2000) { s += a[i][1]; i += 1 }; print(s)
; 
