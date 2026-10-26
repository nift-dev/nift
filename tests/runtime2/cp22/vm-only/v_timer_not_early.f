fn[async](f()) { return 1 }
t0 := epoch()
sleep(120)
print(epoch() - t0 >= 110)
