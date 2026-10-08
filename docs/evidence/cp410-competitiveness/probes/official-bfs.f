grid := []
r := 0
while(r < 20) { row := []; c := 0; while(c < 20) { w := 0; if((r * 7 + c * 13) % 5 == 0 && r > 0 && r < 20 - 1 && c > 0 && c < 20 - 1) { w = 1 }; row.push(w); c += 1 }; grid.push(row); r += 1 }
dist := []
r = 0
while(r < 20) { row := []; c := 0; while(c < 20) { row.push(-1); c += 1 }; dist.push(row); r += 1 }
dist[0][0] = 0
q := [[0, 0]]
qi := 0
goal := 20 - 1
while(qi < q.size()) { cell := q[qi]; qi += 1; cr := cell[0]; cc := cell[1]; if(cr == goal && cc == goal) { break }; dirs := [[1,0],[-1,0],[0,1],[0,-1]]; for(d : dirs) { nr := cr + d[0]; nc := cc + d[1]; if(nr >= 0 && nr < 20 && nc >= 0 && nc < 20 && grid[nr][nc] == 0 && dist[nr][nc] == -1) { dist[nr][nc] = dist[cr][cc] + 1; q.push([nr, nc]) } } }
print(dist[goal][goal])
