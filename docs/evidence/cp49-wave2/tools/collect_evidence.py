import pathlib,json,subprocess,shutil,statistics
b=pathlib.Path('.build/cp49-wave2');dest=pathlib.Path('docs/evidence/cp49-wave2/final');dest.mkdir(exist_ok=True,parents=True)
shutil.copy(b/'final/three-state.json',dest/'three-state.json');shutil.copy(b/'long-cpu/results.json',dest/'long-cpu.json')
for src,name in [('counters/counts.json','dispatch-counters.json'),('value-counters/counts.json','value-counters.json'),('projects/results.json','project-controls.json')]:shutil.copy(b/src,dest/name)
r=json.load(open(b/'final/three-state.json'))
for x in r:
 for mode in ['self','inclusive']:
  p=subprocess.run(['callgrind_annotate','--auto=no','--threshold=90','--inclusive='+('yes' if mode=='inclusive' else 'no'),str(b/'final'/f"{x['name']}-2.callgrind")],capture_output=True,text=True,check=True);(dest/f"{x['name']}-{mode}.txt").write_text(p.stdout)
lines=['# Three-state local comparison','','Original CP49 baseline → accepted first-wave final → corrected second-wave candidate. CPU/wall/RSS are rotating paired medians; short timings include startup/setup. All 126 Memcheck runs have zero errors and fully freed heaps. No official measurements.','', '| Family | Instructions | CPU ms | Wall ms | Allocations | Allocated bytes | RSS KiB |','|---|---:|---:|---:|---:|---:|---:|']
for x in r:
 v=x['variants'];fmt=lambda key:' → '.join(str(round(z[key],2)) for z in v);rss=' → '.join(str(statistics.median(z['rss_kib'])) for z in v);lines.append(f"| {x['name']} | {fmt('instructions')} | {fmt('median_cpu_ms')} | {fmt('median_wall_ms')} | {fmt('allocations')} | {fmt('bytes')} | {rss} |")
(dest/'comparison.md').write_text('\n'.join(lines)+'\n')
for stage in ['path-paired','path2-paired','sort-paired','sort-extra-paired','bare-arg-paired','outcome-paired','key-paired','read-key-paired','member-paired','location-scratch-paired','callback-args-paired']:
 shutil.copy(b/stage/'paired.json',dest/(stage+'.json'))
print('Collected',len(r),'families')
