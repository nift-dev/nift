// Q3 row 4 (§3 Assignment), the read form: a mutation through one alias is seen by the other.
o := {"k": [1]}
p := o
in := p["k"]
in.push(2)
print(o.stringify())
