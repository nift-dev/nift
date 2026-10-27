fn(add(a, b)) { return a + b }
fn(nested(x)) {
    inner := thread(add, x, 1)
    return inner.join()
}
t1 := thread(add, 2, 3)
t3 := thread(nested, 9)
print(t1.join())
print(t1.join())
print(t3.join())
print(t1.done())
print(t1.status())
print(type(t1))
print(t3.join() + t1.join())
