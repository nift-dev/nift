@import("../src/humanize.f")

print(humanize.integer(1234567))
print(humanize.bytes(1536))
print(humanize.plural(2, "person", "people"))
print(humanize.ordinal(23))
