adj := []
i := 0
while(i < 2000) { row := []; if(i+1 < 2000) { row.push(i+1) }; if(i+2 < 2000) { row.push(i+2) }; adj.push(row); i += 1 }
seen := set(); seen.add(0); q := [0]; qi := 0

while(qi < q.size()) { v := q[qi]; qi += 1; for(w : adj[v]) { if(!seen.contains(w)) { seen.add(w); q.push(w) } } }
print(seen.size())

