fn(probe(kind,n)) { print("CALLED"); rows := []; if(kind == "walk") { rows.push([n,1,0]) }; return rows }
p := ["walk","3"]; print(probe(p[0],p[1].to_int()).stringify())
