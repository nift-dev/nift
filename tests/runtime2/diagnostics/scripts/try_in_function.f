fn(safe(path)) {
  try {
    return cat(path)
  } catch(e) {
    return "fallback:" + e.code
  }
}
print(safe("/nonexistent/nift-cp13"))
print("end")
