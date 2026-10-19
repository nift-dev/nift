try {
    import("bad.nft")
} catch(err) {
    print("caught 1: " + err.message)
}
try {
    import("bad.nft")
} catch(err) {
    print("caught 2: " + err.message)
}
print("end")
