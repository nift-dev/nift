// Command-style statements (shell front end, CP19/CP20 scope): no shell, `|`
// is an argument, a missing program exits 127 silently and the script goes on.
print("a")
echo b
print("c")
definitely-missing-cp16 arg
print("d")
ls | wc
print("e")
