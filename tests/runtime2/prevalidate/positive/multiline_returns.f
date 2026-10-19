fn(a()) {
return {
"a": 1,
"b": [2,
3]
}
}
fn(b()) {
return (1 +
2)
}
fn(c()) {
return
print("x")
}
print(a().stringify())
print(b())
c()
print("end")
