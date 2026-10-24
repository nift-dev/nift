try {
  import("init.nft")
} catch(e) {
  print("caught " + e.message + " " + e.line.to_string())
}
print("end")
