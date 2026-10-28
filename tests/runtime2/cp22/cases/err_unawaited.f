fn[async](bad()) { return 1 / 0 }
p := bad()
print("main")
