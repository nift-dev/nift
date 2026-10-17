fn(add(a, b)) { return a + b }
fn(fib(n)) { if(n < 2) { return n } return fib(n - 1) + fib(n - 2) }
i := 0
s := 0
while(i < 20000) { s = add(s, i); i += 1 }
print(s)
print(fib(18))
t := 0
j := 0
while(j < 50000) { t += j; j += 1 }
print(t)
