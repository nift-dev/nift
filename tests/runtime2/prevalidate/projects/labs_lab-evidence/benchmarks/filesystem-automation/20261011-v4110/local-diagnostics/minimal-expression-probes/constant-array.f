fn(probe()) { print("CALLED"); return [["x",1,0]] }
print(probe().stringify())
