// ls(): sorted by bytes; glob display paths relative to the base; dot files.
print(ls("d").stringify())
print(ls("d/*.txt").stringify())
print(ls("d/*").stringify())
print(ls("d/**/*.txt").stringify())
print(ls("../ext/*").stringify())
print(ls(getenv("BASE") + "/ext/*.md").stringify())
print(ls("d/*.none").stringify())
cd("d"); print(ls().stringify()); cd("..")
