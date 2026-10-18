fn(probe()) { print("CALLED"); rows := []; rows.push(["x",1,0]); return rows }
print(probe().stringify())
