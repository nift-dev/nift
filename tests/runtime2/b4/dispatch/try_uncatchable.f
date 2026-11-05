// B4: the failure is an ordinary error of the native class: not catchable (legacy parity;
// coordinator ruling 1, provisional).
fn(t(v)) { print(v.trim()) }
print("before")
t(" x ")
try { t(4) } catch (e) { print("caught " + e.message) }
print("never")
