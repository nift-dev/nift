fn[async](f(x)) { sleep(5); return x + 1 }
t0 := epoch()
p := f(1)
v := await p
print(epoch() - t0 >= 5)
print(v)
