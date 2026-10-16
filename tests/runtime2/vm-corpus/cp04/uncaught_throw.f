fn(bad()) { throw error("boom") }
print("a")
bad()
print("b")
