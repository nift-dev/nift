try {
import("bad.nft")
} catch (e) {
print("caught 1: " + e.message)
}
try {
import("bad.nft")
} catch (e) {
print("caught 2: " + e.message)
}
print("end")
