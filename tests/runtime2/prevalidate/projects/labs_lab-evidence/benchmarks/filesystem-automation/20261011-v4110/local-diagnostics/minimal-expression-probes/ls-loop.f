fn(probe(root)) { print("CALLED"); rows := []; for(fname : ls(root)) { rows.push(fname) }; return rows }
print(probe("/home/nick/Repositories/nift/nift-experiments/filesystem-automation/work/semantics/paths").stringify())
