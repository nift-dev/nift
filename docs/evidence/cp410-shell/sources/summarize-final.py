from pathlib import Path
import json,statistics
p=Path('docs/evidence/cp410-shell')
for source,target,key in [('control-timing.json','control-timing-summary.json','name'),('final-save-timing.json','final-save-summary.json','n')]:
 f=p/source
 if not f.exists():continue
 rows=json.loads(f.read_text());out=[]
 for name in dict.fromkeys(x[key] for x in rows):
  a=[x for x in rows if x[key]==name];med={label:{k:statistics.median(x[k] for x in a if x['label']==label) for k in ('wall','cpu')+ (('rss_kib',) if 'rss_kib' in a[0] else ())} for label in dict.fromkeys(x['label'] for x in a)};out.append({key:name,'medians':med,'candidate_change_pct':{k:100*(med['candidate'][k]/med['baseline'][k]-1) if med['baseline'][k] else None for k in med['baseline']}})
 (p/target).write_text(json.dumps(out,indent=2)+'\n')
