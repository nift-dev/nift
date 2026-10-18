fn(probe(kind,n)) { print("CALLED"); rows := []; if(kind == "walk") { rows.push([n,1,0]) }; return rows }
print(probe("walk",3).stringify())
