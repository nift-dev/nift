import pathlib,re,collections,json,sys
p=pathlib.Path(sys.argv[1]);names={};calls=collections.Counter();own=collections.Counter();arcs=collections.Counter();fn=None;callee=None;arc=False;total=0
for line in p.open():
 if line.startswith('summary:'):total=int(line.split()[1])
 m=re.match(r'(?:c)?fn=\((\d+)\)(?: (.*))?',line)
 if m:
  key=m[1]
  if m[2]:names[key]=m[2]
  if line.startswith('cfn='):callee=key
  else:fn=key
 elif line.startswith('calls='):
  calls[callee]+=int(line.split()[0].split('=')[1]);arc=True
 elif re.match(r'^[+*\-\d]+ ',line) and len(line.split())>=2:
  value=int(line.split()[1])
  if arc:arcs[fn]+=value
  elif fn:own[fn]+=value
  arc=False
patterns=['lexically_normal','lexically_relative','path::operator/=','parent_path','generic_string','_M_split_cmpts','operator new(','basic_string','memcpy','memmove','metadata_path_is_safe','relative_of','load_user_dependencies','build_reasons','ProjectInfo::open','build_many','filesystem::path_within','filesystem::dependency_status','current_hash_cached']
rows=[]
for pattern in patterns:
 matches=[{'function':names[k],'calls':calls[k],'self_instructions':own[k],'inclusive_instructions':own[k]+arcs[k]} for k in names if pattern in names[k]]
 rows.append({'pattern':pattern,'calls':sum(x['calls'] for x in matches),'self_instructions':sum(x['self_instructions'] for x in matches),'inclusive_instructions':sum(x['inclusive_instructions'] for x in matches),'functions':matches})
for name in ['malloc','free']:
 keys=[k for k in names if names[k]==name];rows.append({'pattern':name,'calls':sum(calls[k] for k in keys),'self_instructions':sum(own[k] for k in keys),'inclusive_instructions':sum(own[k]+arcs[k] for k in keys)})
categories=collections.Counter()
for k,v in own.items():
 name=names.get(k,'')
 if 'std::filesystem' in name and ('path::' in name or 'path)' in name):category='filesystem path objects'
 elif any(x in name for x in ['operator new','operator delete','malloc','free','realloc']):category='allocation/destruction excluding paths'
 elif any(x in name for x in ['basic_string','memcpy','memmove','memcmp','strlen','strcmp','char_traits']):category='strings/copies excluding paths'
 elif 'json::' in name:category='JSON parser/document'
 else:category='other/unresolved'
 categories[category]+=v
result={'total_instructions':total,'functions':rows,'self_categories':{k:{'instructions':v,'percent_of_total':100*v/total} for k,v in categories.items()},'resolved_self_instructions':sum(own.values())}
p.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n');print(total,[(r['pattern'],r['calls']) for r in rows])
