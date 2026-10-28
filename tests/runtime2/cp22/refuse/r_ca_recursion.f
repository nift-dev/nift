fn(fib(n)) { if (n < 2) { return n }; return fib(n - 1) + fib(n - 2) }
fn[async](work(n)) { return fib(n) }
print(await work(15))
