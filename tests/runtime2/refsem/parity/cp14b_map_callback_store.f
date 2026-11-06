a:=[1,2,3]; fn(cb(x)) { a[1]=99; return x }; print(a.map(cb).stringify()); print(a.stringify())
