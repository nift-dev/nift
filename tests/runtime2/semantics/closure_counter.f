fn(counter(start)) {
  n := start
  return () => n++
}
c := counter(10)
print(c())
print(c())
d := counter(0)
print(d())
print(c())
