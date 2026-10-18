fn(probe(n)) { print("CALLED"); rows := []; i := 0; while(i < n) { rows.push([i,1,0]); i += 1 }; return rows }
print(probe(3).stringify())
