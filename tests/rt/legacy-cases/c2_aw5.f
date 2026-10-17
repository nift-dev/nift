fn[async](f()) { return 2 }
print(await f() * 3 == 6)
