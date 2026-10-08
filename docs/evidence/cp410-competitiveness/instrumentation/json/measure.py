import pathlib,subprocess,re,json
b=pathlib.Path('.build/cp410/json');result=[]
for shape in ['wide','deep','array','mixed']:
 for mode in ['parse','convert']:
  record=dict(shape=shape,mode=mode);allcounts=[]
  for rounds in [0,10]:
   out=b/(shape+'-'+mode+'-'+str(rounds)+'.callgrind');cmd=[str((b/'probe').resolve()),mode,str(b/(shape+'.json')),str(rounds)];p=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+str(out),*cmd],capture_output=True,text=True,check=True,timeout=600);instructions=int(re.search(r'^summary: (\d+)',out.read_text(),re.M)[1]);p=subprocess.run(['valgrind','--leak-check=full','--error-exitcode=97',*cmd],capture_output=True,text=True,check=True,timeout=600);(b/(shape+'-'+mode+'-'+str(rounds)+'.memcheck')).write_text(p.stderr);assert 'ERROR SUMMARY: 0 errors' in p.stderr;m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',p.stderr);allcounts.append(dict(rounds=rounds,instructions=instructions,allocations=int(m[1].replace(',','')),bytes=int(m[3].replace(',',''))))
  record.update(samples=allcounts,instructions_per_round=(allcounts[1]['instructions']-allcounts[0]['instructions'])/10,allocations_per_round=(allcounts[1]['allocations']-allcounts[0]['allocations'])/10,bytes_per_round=(allcounts[1]['bytes']-allcounts[0]['bytes'])/10);result.append(record);(b/'metrics.json').write_text(json.dumps(result,indent=2)+'\n');print(shape,mode,record['instructions_per_round'],flush=True)
