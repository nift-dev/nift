// file().save() replaces a symlink with a regular file (atomic temp+rename);
// cp() writes through a symlinked destination; rm() removes the link itself.
f := file("link.txt"); f.open("w"); f.write("saved\n"); f.save(); f.close()
cp("src.txt", "link2.txt")
o := ofstream("link3.txt"); o.write("streamed\n"); close(o)
touch("linkdir/inside.txt")
mkdir("linkdir/made")
rm("dangling")
print(is_file("link.txt")); cat("real.txt"); cat("real2.txt"); cat("real3.txt")
print(ls("linkdir").stringify())
