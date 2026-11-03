// B2 review M2: an ordinary fatal error whose text happens to contain the
// sort-protection message stays fatal (the catchable kind is carried by the
// refusal itself, never inferred from a message).
try {
  for (v : ["s"]) { v.push("sort receiver mutated: the array is being sorted by a comparator sort") }
} catch (e) { print("caught " + e.code) }
print("end")
