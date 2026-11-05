// B4: mixed collections dispatch only size and empty; a keyed read stays refused.
x := 2
c := map()
if (x > 1) { c = stack() }
print("before")
print(c.contains(1))
