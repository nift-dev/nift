// B4: a lambda over a union receiver.
first_of := (v) => v.first()
print("before")
print(first_of([9, 8]))
print(first_of("s"))
