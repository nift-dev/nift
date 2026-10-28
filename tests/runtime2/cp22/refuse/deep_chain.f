fn[async](f(n)) { if (n == 0) { return 0 }; x := await f(n - 1); return n + x }
print(await f(50))
