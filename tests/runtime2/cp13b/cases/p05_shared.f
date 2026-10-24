e := error("shared")
try { throw e } catch (c) { print(c.line) }
print(e.line)
