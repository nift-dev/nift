fn(g(n)) {
  return {"a": [1,
    {"b": (n +
      2)}],
    "c": [[1], [2,
      3]]}
}
print(g(1).stringify())
