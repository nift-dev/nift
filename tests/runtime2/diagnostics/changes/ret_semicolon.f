fn(f()) { return {"a": 1}; print("never") }
print(f().stringify())
fn(g()) { return (1 +
  2); print("never") }
print(g())
