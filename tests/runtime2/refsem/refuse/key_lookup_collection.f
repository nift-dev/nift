// D-NEXT-refsem #6 (D6): lookups with a collection key are refused too.
print("start")
s := set()
t := set()
print(s.contains(t))
