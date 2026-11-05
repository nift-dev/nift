// B4 (b4-dispatch.md 6, owner): interpolating a map/set/stack is an ordinary run-time error
// (B0's text, after the earlier output); legacy prints its internal tag, not reproduced.
// Formerly refuse/ (interpolation:unknown-type).
print("ran")
m := map()
print("v=$[m]")
