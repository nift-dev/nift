fn(inner(n)) { return [[n,true]] }
fn(probe(n)) { print("CALLED"); rows := []; for(e : inner(n)) { rows.push([e[0],1,0]) }; return rows }
print(probe(3).stringify())
