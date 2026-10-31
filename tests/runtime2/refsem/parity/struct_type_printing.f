// D-NEXT-refsem: struct values in type/is_struct/print (legacy-equal).
struct(P) { v := 0 }
p := P()
print(type(p))
print(is_struct(p))
print(p)
