fn(add(a, b)) { return a + b }
fn(fib(n)) { if(n < 2) { return n } return fib(n - 1) + fib(n - 2) }
fn(gcd(a, b)) { while(b != 0) { t := b; b = a % b; a = t } return a }
fn(ack(m, n)) {
  if(m == 0) { return n + 1 }
  if(n == 0) { return ack(m - 1, 1) }
  return ack(m - 1, ack(m, n - 1))
}
fn(is_even(n)) { if(n == 0) { return true } return is_odd(n - 1) }
fn(is_odd(n)) { if(n == 0) { return false } return is_even(n - 1) }
fn(nothing()) { }
fn(early(x)) { if(x > 0) { return "pos" } return }
fn(shout(s)) { print(s + "!") }
fn(sum_to(n)) { s := 0; i := 1; while(i <= n) { s += i; i += 1 } return s }
print(add(2, 3))
print(add("a", 1))
print(fib(20))
print(gcd(1071, 462))
print(ack(2, 3))
print(is_even(10))
print(is_odd(7))
print(nothing())
print(early(1))
print(early(-1))
shout("hey")
print(sum_to(100))
print(add(add(1, 2), add(3, 4)))
r := fib(15) + fib(10)
print(r)
