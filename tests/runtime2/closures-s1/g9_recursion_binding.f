fn(down(n)) {
  if (n == 0) { return 0 }
  return k(n - 1)
}
k := down
print(k(63))
print(k(64))
