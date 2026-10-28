fn(bad()) { return 1 / 0 }
t := thread(bad)
print("before")
print(t.join())
print("never")
