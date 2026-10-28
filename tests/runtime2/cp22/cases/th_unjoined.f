fn(bad()) { return 1 / 0 }
fn(late(n)) { sleep(20); print("late " + n); return n }
b := thread(bad)
l := thread(late, 1)
print("main end")
