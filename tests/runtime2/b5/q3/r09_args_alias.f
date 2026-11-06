// Q3 row 9 (§3 args, owner Q1): args is immutable through every alias.
b := args
b.push("q")
print("unreached")
