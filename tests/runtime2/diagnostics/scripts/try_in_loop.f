i := 0
while(i < 3) {
  try {
    if(i == 1) { throw error("odd " + i.to_string()) }
    print("ok " + i.to_string())
  } catch(e) {
    print("caught " + e.message)
  }
  i += 1
}
