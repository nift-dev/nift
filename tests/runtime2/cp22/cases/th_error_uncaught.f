fn(thr()) { throw error("boom", "user.x") }
t := thread(thr)
print("start")
t.join()
print("never")
