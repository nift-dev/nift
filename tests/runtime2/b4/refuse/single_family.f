// B4: a single family without the method stays refused (only unions dispatch).
fn(f(v)) { return v.trim() }
print("before")
print(f(5))
