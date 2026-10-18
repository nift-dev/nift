fn(probe(kind,n)) { print("CALLED"); rows := []; rows.push([kind,n,0]); return rows }
while(true) { line := read(); if(line == null) { break }; p := line.split("|"); result := probe(p[0],p[1].to_int()); print(result.stringify()) }
