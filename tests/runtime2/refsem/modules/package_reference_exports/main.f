// integ/refsem-a (Phase A x packages, D-NEXT-refsem #1-#3): references cross
// the package boundary with their identity. The importer sees the package's
// own map, set, stack and instance (same(), mutations through the package's
// functions are visible); copy() and deepcopy() of an imported map are
// independent of later mutations. Legacy-equal (its collections and structs
// are references too), and Phase B keeps it (no arrays or objects).
import("reg")
print(add(1, 1))
print(registry.size())
print(seen.contains(1))
print(log.size())
print(add(3, 3))
r := registry
print(same(r, registry))
dd := deepcopy(r)
kk := copy(r)
print(add(5, 5))
print(dd.size())
print(kk.size())
print(registry.size())
print(r.size())
print(bump())
print(config.level)
c := config
print(same(c, config))
print(bump())
print(c.level)
