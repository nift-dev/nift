o := {"k": 1}
fn(setk()) {
  o = {"k": [1]}
  return 0
}
export(o)
export(setk)
