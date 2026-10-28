fn[async](f(x)) { sleep(50); print("late " + x); return x }
p := f(1)
print("main")
