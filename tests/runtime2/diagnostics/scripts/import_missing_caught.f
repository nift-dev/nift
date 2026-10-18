try {
  import("lib/missing_module.f")
} catch(e) {
  print(e.code)
}
