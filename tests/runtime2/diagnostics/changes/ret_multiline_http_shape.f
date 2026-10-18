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
