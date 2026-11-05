// B4 C1: a variable declared after the import: legacy reports a redeclaration; refused.
import("m.f")
mf := 1
print(mf)
