import pathlib,json,subprocess,re,sys
b=pathlib.Path('.build/cp410');root=b/'scaling';root.mkdir(exist_ok=True);rows=json.loads((b/'probes.json').read_text());names=['loops','call-scalar','frequency','map-set','sliding-window','bfs','json-traverse','json-mutate','filesystem'];out=[]
for r in rows:
 if r['name'] not in names:continue
 record=dict(name=r['name'],n=r['n'])
 for tag,binary in [('baseline',b/'baseline/nift'),('final',b/'prefix-nift')]:
  p=root/f"{r['name']}-{r['n']}-{tag}.callgrind";q=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(p),str(binary.resolve()),r['path']],text=True,capture_output=True,check=True);assert q.stdout.strip()==r['expected'];record[tag]=int(re.search(r'^summary: (\d+)',p.read_text(),re.M)[1])
 out.append(record);(root/'metrics.json').write_text(json.dumps(out,indent=2)+'\n');print(record,flush=True)
