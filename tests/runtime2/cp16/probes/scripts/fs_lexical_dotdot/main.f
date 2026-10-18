// `link/..` is resolved lexically (CP02 10a): link/../u.txt names ./u.txt, while
// the OS (cat, sh) resolves it to real/u.txt.
print(open("link/../u.txt"))
print(run("cat", "link/../u.txt").stdout)
print(exists("link/../inner"))
