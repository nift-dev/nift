fn(h(o)) {
return o?["a"]?.b ?? 0
}
i := 0
while (i < 2) {
i += 1
print(h({"a": {"b": i}}))
print(h(null))
print(null?["x"])
}
