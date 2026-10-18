fn(work(n)) { return n + undefined_name }
t := thread(work, 1)
print(t.join())
