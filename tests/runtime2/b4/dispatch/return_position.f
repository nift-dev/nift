// B4: a failure inside a return expression is reported at the failing call, without legacy's
// "return: " prefix: the approved D-009#5 change, as for every native error (golden .vm).
fn(f(v)) { return v.trim() }
print("before")
print(f(" a "))
print(f(5))
