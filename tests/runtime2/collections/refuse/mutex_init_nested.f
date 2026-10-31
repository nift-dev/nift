print("ran")
mx := mutex([1])
fn(w()) { return 0 }
m := map()
mx2 := mutex([[m]])
print("made")
