// exists/is_file/is_dir/stat on every kind of path; symlinks are followed.
names := ["file.txt", "dir", "lfile", "ldir", "dangling", "chain", "fifo", "missing", "dir/", "./file.txt", "dir/../file.txt"]
for(n : names) {
    print(n + " " + exists(n).to_string() + " " + is_file(n).to_string() + " " + is_dir(n).to_string() + " " + stat(n).stringify())
}
fo := file("file.txt"); print(fo.exists()); print(fo.path() == getenv("BASE") + "/w/file.txt")
print(exists("~/nothing-here"))
print(exists("~"))
