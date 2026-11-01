// integ/refsem-a (Phase A x coro-module, D-NEXT-refsem #5 / D7): an async
// task given an imported module's map receives a copy made at creation. The
// task's mutation does not reach the module's map, and the importer's later
// mutation does not reach the task's copy. Legacy refuses the map as a
// non-transferable resource (an approved difference); weekend/r2 refused the
// unit (coro-module-capture; it has no map(), callable:map).
// The cooperative scheduler keeps the coro-module refusal for linked units.
import("m.f")
fn[async](work(m)) {
  m.set("w", 2)
  return m
}
f := work(shared)
shared.set("late", 3)
r := await f
print(r.size())
print(shared.size())
print(same(r, shared))
print(r.contains("late"))
print(shared.contains("w"))
