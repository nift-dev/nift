import pathlib,json,hashlib,subprocess
b=pathlib.Path('.build/cp410');before=json.loads((b/'frozen-before.json').read_text());root=pathlib.Path('/home/nick/Repositories/nift');out={};fail=[]
for name,v in before.items():
 p=root/name if name!='official_campaign' else root/'scripting-benchmark/evidence/campaign-20261009-v490'
 actual={};missing=[];changed=[]
 for relative,digest in v['hashes'].items():
  q=p/relative
  if not q.is_file():missing.append(relative);continue
  h=hashlib.sha256(q.read_bytes()).hexdigest();actual[relative]=h
  if h!=digest:changed.append(relative)
 if name=='official_campaign':
  added=sorted(str(q.relative_to(p)) for q in p.rglob('*') if q.is_file() and str(q.relative_to(p)) not in v['hashes']);head=None
 else:
  tracked=subprocess.check_output(['git','ls-files'],cwd=p,text=True).splitlines();added=sorted(q for q in set(tracked)-v['hashes'].keys() if (p/q).is_file());head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=p,text=True).strip()
 ok=not missing and not changed and not added and (head==v.get('head'))
 out[name]=dict(unchanged=ok,files=len(actual),head=head,missing=missing,changed=changed,added=added)
 relevant=lambda path:any(key in path for key in ['scripting','shell','v490','20261009','benchmarks/evidence'])
 if name in ['lab','lab/public']:
  changes=missing+changed+added
  original_oracle_changes=[path for path in missing+changed if relevant(path)]
  presentations={'content/benchmarks/shell/index.html','templates/benchmarks/shell/template.html','benchmarks/shell/index.html'}
  result_changes=[path for path in original_oracle_changes if path not in presentations]
  out[name]['frozen_result_material_unchanged']=not result_changes
  out[name]['scripting_presentation_unchanged']=not any('scripting' in path for path in changes)
  out[name]['shell_presentation_unchanged']=not any(path in presentations for path in changes)
  out[name]['oracle_files_checked']=sum(relevant(path) for path in actual)
  out[name]['oracle_original_changes']=original_oracle_changes
  out[name]['result_issues']=result_changes
  out[name]['concurrent_untracked']=subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=p,text=True).splitlines()
  if result_changes:fail.append(name)
 elif not ok:fail.append(name)
(b/'frozen-after.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2));assert not fail,fail
