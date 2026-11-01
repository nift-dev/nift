a:=[1,2,3]; fn(cb(x)) { a.pop(); a.push(9); return x }; print(a.map(cb).stringify()); print(a.stringify())
