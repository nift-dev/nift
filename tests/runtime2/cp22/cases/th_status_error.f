fn(bad()) { return 1 / 0 }
t := thread(bad)
sleep(50)
print(t.status())
print(t.done())
