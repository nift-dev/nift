import pathlib,subprocess,json,time
out=pathlib.Path('.build/cp49-campaign/final');results=[]
for label,binary in [('original','.build/cp49-campaign/original-san/nift'),('final','.build/nift-sanitize')]:
 started=time.monotonic()
 with (out/('fuzz-'+label+'-paired.log')).open('w') as log:
  p=subprocess.run(['python3','scripts/checkpoint9_parser_fuzz.py','--nift',str(pathlib.Path(binary).resolve()),'--cases','400','--seeds','9001,17713,424242','--output',str(out/('parser-fuzz-'+label+'.json'))],stdout=log,stderr=subprocess.STDOUT)
 results.append({'variant':label,'exit':p.returncode,'seconds':time.monotonic()-started});(out/'paired-fuzz-results.json').write_text(json.dumps(results,indent=2)+'\n');print(label,p.returncode,flush=True)

raise SystemExit(0 if all(r["exit"] == 0 for r in results) else 1)
