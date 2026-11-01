// integ/refsem-a (Phase A x packages): a package exports a map, a set, a
// stack, a struct instance and functions that mutate them.
struct(Cfg) {
  level := 1
}
registry := map()
seen := set()
log := stack()
config := Cfg()
fn(add(k, v)) {
  registry.set(k, v)
  seen.add(k)
  log.push(k)
  return registry.size()
}
fn(bump()) {
  config.level = config.level + 1
  return config.level
}
export(registry)
export(seen)
export(log)
export(config)
export(add)
export(bump)
