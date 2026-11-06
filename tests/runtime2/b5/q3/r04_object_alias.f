// Q3 row 4 (§3 Assignment): p and o are one object (the keyed store form is a pre-existing
// VM refusal, index-target, before any effect; the member read form below shows the sharing).
o := {"k": 1}
p := o
p["k"] = 2
print(o.stringify())
