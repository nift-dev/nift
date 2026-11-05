// B3 (b3-structs.md): the nift-packages/http capabilities() shape (from
// diagnostics/changes/), formerly pending: Phase A refused it
// (presentation-struct-contents:stringify) though it holds no instance; it is
// now a parity case.
struct(Backend) {
  locked := true
  fn(capabilities()) {
    if(locked) {
      return {
        "buffered_text":true,
        "json":true,
        "streaming":false
      }
    }
    return {"ok":true}
  }
}
b := Backend()
print(b.capabilities().stringify())
