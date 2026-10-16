fn[async](work(n)) { return n * 2 }
fu := work(21)
print(fu.await())
