fn(probe(kind,root,dest,n,variant)) { print("CALLED"); rows := []; rows.push([kind,n,variant]); return rows }
while(true) { line := read(); if(line == null) { break }; p := line.split("|"); print(probe(p[0],p[1],p[2],p[3].to_int(),p[4].to_int()).stringify()) }
