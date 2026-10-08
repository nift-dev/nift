import json,pathlib,subprocess,sys,os
b=pathlib.Path('.build/cp410');root=b/'contracts';root.mkdir(exist_ok=True);rows=json.load(open('.build/cp49-identity/contracts/results.json'));results=[]
for row in rows:
 source=root/(row['name']+'.f');source.write_text(row['source']);before=subprocess.run([str((b/'baseline/nift').resolve()),str(source)],text=True,capture_output=True);after=subprocess.run([str(pathlib.Path(sys.argv[1]).resolve()),str(source)],text=True,capture_output=True);assert (before.returncode,before.stdout,before.stderr)==(after.returncode,after.stdout,after.stderr),(row['name'],before.stderr,after.stderr);assert (before.stdout==row['expected'] if isinstance(row['expected'],str) else row['expected']['error'] in before.stderr if isinstance(row['expected'],dict) else True),row['name'];assert 'AddressSanitizer' not in after.stderr and 'runtime error:' not in after.stderr
 results.append(dict(name=row['name'],exit=after.returncode,stdout=after.stdout,stderr=after.stderr,exact_baseline_match=True))
(b/('contracts-'+sys.argv[2]+'.json')).write_text(json.dumps(results,indent=2)+'\n');print('PASS',len(results),'exact factory/capture/identity/lifetime/async contracts')
