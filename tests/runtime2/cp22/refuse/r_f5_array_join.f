fn[async](j(xs)) { return xs.join("-") }
print(await j(["a", "b", "c"]))
