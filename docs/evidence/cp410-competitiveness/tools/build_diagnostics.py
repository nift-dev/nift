import pathlib,json,subprocess
for family in ['phases','lifecycle']:
 p=pathlib.Path('.build/cp410')/family
 with (p/'build.log').open('w') as log:
  for c in json.loads((p/'build.json').read_text()):
   print('BUILD',family,c[-1],flush=True);subprocess.run(c,stdout=log,stderr=subprocess.STDOUT,check=True)
 print('PASS diagnostic build',family,flush=True)
