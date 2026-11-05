// B4: the receiver is evaluated exactly once, in the success and the failure case,
// and the arguments of an all-natives union are evaluated once, before the call.
fn(pick(i)) {
  print("pick " + i)
  if (i == 0) { return "xa" }
  return ["a", "b"]
}
fn(other(i)) {
  print("other " + i)
  if (i == 0) { return " a " }
  return 7
}
fn(arg()) { print("arg"); return "a" }
print("before")
for (i : [0, 1]) { print(pick(i).contains(arg())) }
print(other(0).trim())
print(other(1).trim())
print("never")
