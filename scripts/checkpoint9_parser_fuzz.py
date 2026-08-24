#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, os, pathlib, platform, random, subprocess, tempfile, time

ap=argparse.ArgumentParser()
ap.add_argument('--nift',required=True)
ap.add_argument('--cases',type=int,default=400,help='generated mutations per seed')
ap.add_argument('--seeds',default='9001,17713,424242')
ap.add_argument('--timeout',type=float,default=3.0)
ap.add_argument('--output',required=True)
a=ap.parse_args()
NIFT=str(pathlib.Path(a.nift).resolve())
repo=pathlib.Path(__file__).resolve().parents[1]

def commit():
    try:return subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True,stderr=subprocess.DEVNULL).strip()
    except Exception:return 'unknown'

def sanitizer_finding(text):
    needles=('ERROR: AddressSanitizer','runtime error:','LeakSanitizer:',
             'UndefinedBehaviorSanitizer','AddressSanitizer:DEADLYSIGNAL')
    return next((x for x in needles if x in text),None)

def mutate(rng,s):
    alphabet="@#$[](){}'\"\\<>!-_:;,. abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\n\t"
    for _ in range(1+rng.randrange(5)):
        op=rng.randrange(7)
        if op==0 and s:
            i=rng.randrange(len(s)); s=s[:i]+s[i+1:]
        elif op==1:
            i=rng.randrange(len(s)+1); s=s[:i]+rng.choice(alphabet)+s[i:]
        elif op==2 and s:
            i=rng.randrange(len(s)); s=s[:i]+rng.choice(alphabet)+s[i+1:]
        elif op==3 and s:
            i=rng.randrange(len(s)); j=min(len(s),i+1+rng.randrange(max(1,min(24,len(s)-i))))
            s=s[:j]+s[i:j]+s[j:]
        elif op==4 and len(s)>2:
            i=rng.randrange(len(s)); j=rng.randrange(i,len(s))
            s=s[:i]+s[j:]
        elif op==5:
            i=rng.randrange(len(s)+1)
            token=rng.choice(['@if(true){','@if(false){','}else{','@content','$[title]','@# fuzz\n',
                              '<#-- fuzz } --#>','<!-- fuzz } -->','\\@','\\$','{}','()','[]'])
            s=s[:i]+token+s[i:]
        else:
            i=rng.randrange(len(s)+1); s=s[:i]+''.join(rng.choice(alphabet) for _ in range(rng.randrange(1,12)))+s[i:]
    return s

BASES=[
'''@json("data/site.json", site)
@if(site.enabled){<b>$[title]</b>}else{<i>$[name]</i>}
@for(item : site.items){<span>$[item.name]-$[loop.index]</span>}
@content
''',
'''<main>
@# line comment } { @if(
<#-- raw comment @if(false){ } --#>
<!-- html comment @for(x:y){ } -->
@getenv("HOME")
@ent("&")
@content
</main>
''',
'''@if(true){
  @if(false){bad}else if(true){ok}else{bad2}
  @input("fragment.html")
}
<a href="@pathto('public/assets/a.txt')">$[title]</a>
@content
''',
'''<pre><code>@if(true){literal-ish}
$[title]
</code></pre>
@content
''',
'''@json('data/site.json',site)
@if(site.count >= 2){yes}
@if(site.name == "Nift"){name}
@for((key,val):site.object){$[key]=$[val]}
@content
'''
]

def setup(root):
    subprocess.run([NIFT,'init'],cwd=root,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=SAN_ENV)
    tr=json.loads((root/'.nift/tracked.json').read_text())
    tr['tracked']=[{'name':'/','title':'Fuzz','template':'templates/template.html'}]
    (root/'.nift/tracked.json').write_text(json.dumps(tr,separators=(',',':'))+'\n')
    (root/'content/index.html').write_text('<p>CONTENT</p>\n')
    (root/'templates/fragment.html').write_text('<small>$[name]</small>\n')
    (root/'data').mkdir(exist_ok=True)
    (root/'data/site.json').write_text('{"enabled":true,"count":3,"name":"Nift","items":[{"name":"a"},{"name":"b"}],"object":{"x":1,"y":2}}\n')
    (root/'public/assets').mkdir(parents=True,exist_ok=True)
    (root/'public/assets/a.txt').write_text('asset\n')

def run_case(root,text,case_id,timeout):
    (root/'templates/template.html').write_text(text)
    started=time.monotonic()
    try:
        p=subprocess.run([NIFT,'build', '--all'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                         timeout=timeout,env=SAN_ENV)
    except subprocess.TimeoutExpired:
        raise RuntimeError(f'case {case_id} timed out')
    elapsed=time.monotonic()-started
    combined=p.stdout+'\n'+p.stderr
    finding=sanitizer_finding(combined)
    if finding:
        raise RuntimeError(f'case {case_id} sanitizer finding {finding}:\n{combined[-4000:]}')
    if p.returncode < 0:
        raise RuntimeError(f'case {case_id} terminated by signal {-p.returncode}:\n{combined[-2000:]}')
    return p.returncode,elapsed

SAN_ENV=os.environ.copy()
SAN_ENV.pop('LD_PRELOAD',None)
SAN_ENV['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1:abort_on_error=1'
SAN_ENV['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'

seeds=[int(x) for x in a.seeds.split(',') if x.strip()]
started=time.monotonic()
successful=0; controlled_errors=0; max_elapsed=0.0
boundary_results=[]
with tempfile.TemporaryDirectory(prefix='nift-cp9-fuzz-') as td:
    root=pathlib.Path(td); setup(root)
    for seed in seeds:
        rng=random.Random(seed)
        for i in range(a.cases):
            source=mutate(rng,rng.choice(BASES))
            rc,elapsed=run_case(root,source,f'fuzz-{seed}-{i}',a.timeout)
            max_elapsed=max(max_elapsed,elapsed)
            successful += rc==0
            controlled_errors += rc!=0

    boundaries=[]
    for depth in (16,32,63,64,65,80,128):
        boundaries.append((f'nested-if-{depth}','@if(true){'*depth+'X'+'}'*depth+'\n@content\n',5.0))
    boundaries += [
      ('literal-1m','x'*(1024*1024)+'\n@content\n',8.0),
      ('literal-8m','x'*(8*1024*1024)+'\n@content\n',15.0),
      ('raw-comment-4m','<#--'+'x'*(4*1024*1024)+'--#>\n@content\n',12.0),
      ('html-comment-4m','<!--'+'}'*(4*1024*1024)+'-->\n@content\n',12.0),
      ('line-comment-2m','@# '+'x'*(2*1024*1024)+'\n@content\n',10.0),
      ('parameter-1m','@getenv("'+'A'*(1024*1024)+'")\n@content\n',10.0),
      ('balanced-parens-100k','@if('+'('*50000+'true'+')'*50000+'){ok}\n@content\n',12.0),
      ('interpolation-1m','$['+'a'*(1024*1024)+']\n@content\n',10.0),
      ('unicode-volume',('λ日本語🙂é'*120000)+'\n@content\n',12.0),
    ]
    for name,source,to in boundaries:
        rc,elapsed=run_case(root,source,name,to)
        max_elapsed=max(max_elapsed,elapsed)
        successful += rc==0
        controlled_errors += rc!=0
        if name.startswith('nested-if-'):
            depth=int(name.rsplit('-',1)[1])
            if depth <= 64 and rc != 0:
                raise RuntimeError(f'{name} should remain within the 64-level parser depth boundary')
            if depth > 64 and rc == 0:
                raise RuntimeError(f'{name} unexpectedly exceeded the parser depth boundary')
        boundary_results.append({'name':name,'exit':rc,'elapsed_seconds':round(elapsed,6)})

    (root/'templates/template.html').write_text('<main>@content</main>\n')
    (root/'content/index.html').write_text(('content-$[title]-'*400000)+'\n')
    rc,elapsed=run_case(root,'<main>@content</main>\n','content-6m',15.0)
    max_elapsed=max(max_elapsed,elapsed)
    successful += rc==0; controlled_errors += rc!=0
    boundary_results.append({'name':'content-6m','exit':rc,'elapsed_seconds':round(elapsed,6)})
    boundary_count=len(boundaries)+1

out=pathlib.Path(a.output); out.parent.mkdir(parents=True,exist_ok=True)
generated_total=a.cases*len(seeds)
data={'schema_version':1,'checkpoint':'9-parser-fuzz','commit':commit(),'platform':platform.platform(),
      'seeds':seeds,'generated_cases_per_seed':a.cases,'generated_cases':generated_total,'boundary_cases':boundary_count,
      'boundary_results':boundary_results,'total_cases':generated_total+boundary_count,'successful_builds':successful,
      'controlled_errors':controlled_errors,'timeouts':0,'crashes':0,'sanitizer_findings':0,
      'max_case_seconds':round(max_elapsed,6),'elapsed_seconds':round(time.monotonic()-started,3),
      'pass':True}
out.write_text(json.dumps(data,indent=2)+'\n')
print(f"checkpoint 9 parser fuzz/resource: PASS ({data['total_cases']} cases; {successful} builds, {controlled_errors} controlled errors)")
print('evidence='+str(out))
