import pathlib,json,collections,re,os,subprocess
p=pathlib.Path('/tmp/nift-noop-investigation');result={};d=p/'fixture-10000-nift';m=json.loads((d/'manifest.json').read_text());result['topology']={'actions':len(m['actions']),'completion_edges':sum(len(a['prerequisites']) for a in m['actions']),'declared_input_occurrences':sum(len(a['inputs']) for a in m['actions']),'unique_declared_inputs':len(set(x for a in m['actions'] for x in a['inputs']))}
files={}
for pattern in ['.nift/tracked.json','.nift/config.json','**/*.info.json','**/*.deps.json','.nift/**/*.hash']:
 paths=list(d.glob(pattern));files[pattern]={'files':len(paths),'bytes':sum(x.stat().st_size for x in paths)}
result['state_files']=files
for x in [d/'.nift/tracked.json',next(d.glob('**/*.info.json'))]:print(x.relative_to(d),x.stat().st_size)
for system in ['nift','ninja']:
 for size in [100,1000,5000,10000]:
  stats={}
  for line in (p/f'strace-{size}-{system}.txt').read_text().splitlines()[2:]:
   a=line.split()
   if len(a)>=5 and re.fullmatch('[0-9.]+',a[0]):stats[a[-1]]=int(a[3])
  result[f'syscalls-{size}-{system}']=stats
freq=collections.Counter()
for line in (p/'paths-100-nift.txt').read_text().splitlines():
 match=re.search(r'newfstatat\([^,]*, "([^"]*)"',line)
 if match:freq[match[1]]+=1
result['targeted100_stat_top']=freq.most_common(20);result['targeted100_unique_named_stat_paths']=len(freq)
(p/'details.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result['topology']));print(json.dumps(files))
