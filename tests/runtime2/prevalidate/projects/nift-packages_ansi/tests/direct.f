@import("../src/ansi.f")

esc := bytes([27]).decode("utf-8")
print(ansi.sgr([1, 31]) == esc + "[1;31m")
print(ansi.wrap("direct", 36) == esc + "[36mdirect" + esc + "[0m")
