// D-NEXT-refsem #6 (D6): a collection as a set member is refused (also s.add(s)).
print("start")
s := set()
s.add(s)
