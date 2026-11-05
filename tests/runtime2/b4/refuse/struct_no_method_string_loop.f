// B4 review H1: a struct calling a string method's name it does not declare, in loop code:
// legacy's prepared evaluator runs the string native on the instance's tag text (14 / nif),
// not "struct has no method": refused before any effect (struct-no-method), as in B3.
struct(S) { a := 1 }
c := S()
print("before")
for (i : [1]) { print(c.length()) }
print(c.substr(1, 3))
print("after")
