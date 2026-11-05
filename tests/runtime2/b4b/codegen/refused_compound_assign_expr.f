// P1 audit (refused before any effect: expression:assignment-target / index-target): compound assignment used as a value (a name, an element), constant operand.
x := 1
print(x += 4)
xs := [1]
print(xs[0] += 4)
