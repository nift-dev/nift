i := 0
es := [{"key": "x", "value": 0}]
o := es.from_entries()
while (i < ROUNDS) {
  es = [{"key": "a-fairly-long-generated-key-" + i, "value": 0}]
  o = es.from_entries()
  i += 1
}
print(i)
