fn(id(x)) { return x }
fn(boom()) { throw error("arg") }
try {
  print(id(boom()))
} catch(e) { print("caught " + e.line.to_string() + ":" + e.column.to_string()) }
