o := {
"a": {"b": [1,
2]}
}
print(o?["a"]?.b?[1])
fn(r()) {
return {
"x": [1,
{"y": 5}]
}
}
print(r()?["x"]?[1]?.y)
