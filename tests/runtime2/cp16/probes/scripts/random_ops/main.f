a := secure_random_bytes(32); b := secure_random_bytes(32)
print(type(a)); print(a.length()); print(a == b); print(secure_random_bytes(0).length())
print(secure_random_bytes(10000000).length())
try { secure_random_bytes(10000001) } catch(e) { print("caught " + e.code) }
