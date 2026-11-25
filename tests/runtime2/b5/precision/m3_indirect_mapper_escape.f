print("marker")
rows := [{"v":3}]
callbacks := []
for(row : rows) {
    mapper := x => row.v+x
    later := x => [x].map(mapper)
    rows.clear()
    callbacks.push(later)
    break
}
