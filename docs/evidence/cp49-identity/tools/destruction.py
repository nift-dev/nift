"""Unmodified production Parser destructor scope via Callgrind function toggling."""
import pathlib,json,subprocess,re
b=pathlib.Path('.build/cp49-identity');binary=str(pathlib.Path('nift').resolve());out=[]
for r in json.loads((b/'probes.json').read_text()):
 path=b/(r['name']+'-destruction.callgrind');p=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--toggle-collect=Parser::~Parser()','--callgrind-out-file='+str(path),binary,r['path']],capture_output=True,text=True,check=True,timeout=300);assert '\n'.join(p.stdout.splitlines()[:-1])==r['expected'];ir=int(re.search(r'^summary: (\d+)',path.read_text(),re.M)[1]);assert ir
 a=subprocess.run(['callgrind_annotate','--inclusive=yes','--auto=no',str(path)],capture_output=True,text=True,check=True);(b/(r['name']+'-destruction.txt')).write_text(a.stdout)
 p=subprocess.run(['callgrind_annotate','--inclusive=yes','--auto=no','--show=totB,totBk,totFdB,totFdBk','--sort=totFdB',str(b/(r['name']+'.xtree'))],capture_output=True,text=True,check=True);(b/(r['name']+'-frees.txt')).write_text(p.stdout)
 destructor=[line for line in p.stdout.splitlines() if line.rstrip().endswith('Parser::~Parser()')];out.append(dict(name=r['name'],instructions=ir,allocation_annotation=destructor));print(r['name'],ir,destructor,flush=True)
(b/'destruction.json').write_text(json.dumps(out,indent=2)+'\n')
