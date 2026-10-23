fa := () => null
fb := () => null
for (e : [1,2]) { if (e == 1) { fa = () => e } else { fb = () => e } }
print(fa())
print(fb())
