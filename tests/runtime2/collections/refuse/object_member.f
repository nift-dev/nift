// D-NEXT-refsem review round 1: an object member is refused as collection-object-key
// (it was reported as collection-identity-key, "a struct or handle").
print("ran")
s := set()
print(s.contains({"a": 1}))
