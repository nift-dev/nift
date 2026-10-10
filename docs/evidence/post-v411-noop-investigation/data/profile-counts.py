import re,collections,json,pathlib
p=pathlib.Path('/tmp/nift-noop-investigation');names={};calls=collections.Counter();selfir=collections.Counter();cfn=None;fn=None;skip=False
for line in (p/'callgrind.out').open():
 m=re.match(r'(?:c)?fn=\((\d+)\)(?: (.*))?',line)
 if m:
  key=m[1]
  if m[2]:names[key]=m[2]
  if line.startswith('cfn='):cfn=key
  else:fn=key
 elif line.startswith('calls='):
  calls[cfn]+=int(line.split()[0].split('=')[1]);skip=True
 elif re.match(r'^[+*\-\d]+ ',line) and len(line.split())>=2:
  if not skip and fn:selfir[fn]+=int(line.split()[1])
  skip=False
patterns=['operator new(','malloc','free','lexically_normal','lexically_relative','path::operator/=','generic_string','parse_string','_M_split_cmpts','_Rb_tree','build_many','build_reasons','load_tracking']
rows=[]
for pat in patterns:
 matches=[{'function':names.get(k,k),'calls':calls[k],'self_instructions':selfir[k]} for k in names if pat in names[k]]
 rows.append({'pattern':pat,'call_count':sum(x['calls'] for x in matches),'self_instructions':sum(x['self_instructions'] for x in matches),'functions':matches})
(p/'profile-counts.json').write_text(json.dumps(rows,indent=2)+'\n')
for r in rows:print(r['pattern'],r['call_count'],r['self_instructions'])
