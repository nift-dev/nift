// B4 C1: a function declared after the import: legacy rebinds silently; refused (the 006
// shape of the CP25-R corpus).
import("m.f")
fn(mf()) { return 1 }
print("after")
