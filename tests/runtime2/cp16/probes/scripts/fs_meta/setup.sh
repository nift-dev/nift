printf 'x' > file.txt; mkdir dir; ln -s file.txt lfile; ln -s dir ldir; ln -s nowhere dangling; ln -s lfile chain
mkfifo fifo 2>/dev/null || true
