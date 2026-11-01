// Integration review (weekend/r2 8b3cd02f runs it): a presentation method keeps its
// string result on a receiver that is only possibly a collection (D-063 widened `any`
// with the collection kinds; `e.stringify().length()` was refused as type:length). map.
m := map()
m.set("a", 1)
print(m.stringify().length())
