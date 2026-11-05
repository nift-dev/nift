// B4: arguments of a call whose union has a failing family are unobservable (a name,
// a constant): legacy fails before evaluating them, and so does the VM.
sep := ","
print("before")
for (v : [[1, 2], "ab"]) { print(v.join(sep)) }
