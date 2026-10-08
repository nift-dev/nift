from pathlib import Path
import json
base=Path(__file__).resolve().parents[4]/'.build/cp49'
probes=base/'probes';probes.mkdir(parents=True,exist_ok=True)
records=[]
def add(name,n,setup,body,out):
 p=probes/f'{name}-{n}.f'
 p.write_text(setup+'\nprobe_timer := timer(); probe_timer.start()\n'+body+'\nprobe_timer.stop(); print(probe_timer.elapsed())\n')
 records.append(dict(name=name,n=n,path=str(p),expected=out))
def loop(n,body):return f'i := 0\nwhile(i < {n}) {{ {body}; i += 1 }}'
for n in [2000,4000,8000,16000]:
 setup=loop(n,'a.push(i)')
 add('loops',n,'s := 0',loop(n,'s += i')+'\nprint(s)',str(n*(n-1)//2))
 add('array-build',n,'a := []',setup+'\nprint(a.size())',str(n))
 for name,fn,body,out in [
 ('call-noarg','fn(f()) { return 1 }','s += f()',n),
 ('call-scalar','fn(f(x)) { return x + 1 }','s += f(i)',n*(n+1)//2),
 ('call-multi','fn(f(x,y,z)) { return x+y+z }','s += f(i,1,2)',n*(n-1)//2+3*n),
 ('call-locals','fn(f(x)) { a := x + 1; b := a * 2; return b }','s += f(i)',n*(n+1)),
 ('call-recursion','fn(f(x)) { if(x == 0) { return 0 }; return 1 + f(x-1) }','s += f(3)',3*n),
 ('call-closure','offset := 1\nf := x => x + offset','s += f(i)',n*(n+1)//2),
 ('call-callback','fn(apply(f,x)) { return f(x) }\nf := x => x + 1','s += apply(f,i)',n*(n+1)//2)]:
  add(name,n,fn+'\ns := 0',loop(n,body)+'\nprint(s)',str(out))
 arr='a := []\n'+setup
 add('array-index',n,arr+'\ns := 0',loop(n,'s += a[i]').replace('i := 0','i = 0')+'\nprint(s)',str(n*(n-1)//2))
 for name,key in [('sort-identity','x'),('sort-index','a[x]'),('sort-arithmetic','x + 1')]:
  add(name,n,'a := []\n'+loop(n,f'a.push((i*7919) % {n})'), 'b := a.sort_by(x => '+key+')\nprint(b.size())',str(n))
 for name,key in [('map-identity','x'),('map-index','a[x]'),('map-arithmetic','x+1')]:
  add(name,n,arr,'b := a.map(x => '+key+'); print(b.size())',str(n))
 add('frequency',n,'m := map()',loop(n,'k := (i % 37).to_string(); v := 0; if(m.contains(k)) { v = m.get(k) }; m.set(k,v+1)')+'\nprint(m.size())','37')
 add('map-set',n,'m := map(); s := set()',loop(n,'m.set(i,i+1); s.add(i)')+'\nprint(m.size())',str(n))
 add('sliding-window',n,arr+'\ns := 0',loop(n-8,'s += a[i+7] - a[i]').replace('i := 0','i = 0')+'\nprint(s)',str((n-8)*7))
 js=probes/f'records-{n}.json';js.write_text(json.dumps([{'k':i,'v':i%7} for i in range(n)])); path=json.dumps(str(js))
 add('json-parse-convert',n,'',f'a := inject({path}); print(a.size())',str(n))
 loaded=f'a := inject({path})\ns := 0'
 add('json-traverse',n,loaded,'for(e : a) { s += e.v }; print(s)',str(sum(i%7 for i in range(n))))
 add('json-mutate',n,loaded,'for(e : a) { e["total"] = e.v * 2; s += e["total"] }; print(s)',str(2*sum(i%7 for i in range(n))))
 add('json-serialize',n,loaded,'serialized := a.stringify(); print(serialized.length() > 0)','true')
 add('construct-destroy',n,'',loop(n,'x := {"a":i,"b":[i,i+1]}')+f'\nprint({n})',str(n))
 # Mixed graph traversal: a chain with two forward edges, no special shortcut.
 grid='adj := []\n'+loop(n,f'row := []; if(i+1 < {n}) {{ row.push(i+1) }}; if(i+2 < {n}) {{ row.push(i+2) }}; adj.push(row)')
 add('bfs',n,grid+'\nseen := set(); seen.add(0); q := [0]; qi := 0',f'while(qi < q.size()) {{ v := q[qi]; qi += 1; for(w : adj[v]) {{ if(!seen.contains(w)) {{ seen.add(w); q.push(w) }} }} }}\nprint(seen.size())',str(n))
 tree=probes/f'tree-{n}';tree.mkdir(exist_ok=True)
 for i in range(n):
  d=tree/str(i//100);d.mkdir(exist_ok=True);(d/f'{i}.txt').touch()
 add('filesystem',n,'',f'a := ls({json.dumps(str(tree)+"/**/*.txt")}); print(a.size())',str(n))
add('empty',0,'','','')
(base/'probes.json').write_text(json.dumps(records,indent=2)+'\n')
print(len(records),'independent local probes')
