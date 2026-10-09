from pathlib import Path
import subprocess,json,re
p=Path(__file__).parent.resolve();rows=[]
for name,text,old,new in [('ascii-no-match','abc_def','z','q'),('ascii-many','a'*128,'A','xy'),('utf8-short','é_λ_Z','é','ø'),('utf8-large','é_λ_Z'*2048,'_','__')]:
 code=f's := {json.dumps(text,ensure_ascii=False)}; i := 0; while(i < 1000) {{ s={json.dumps(text,ensure_ascii=False)}.to_upper().replace({json.dumps(old,ensure_ascii=False)},{json.dumps(new,ensure_ascii=False)}); i+=1 }}; print(s)';expected=None
 for label in ('baseline','string'):
  stem=f'string-{name}-{label}';cg=p/(stem+'.callgrind');r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(p/(label+'-nift')),'-e',code],capture_output=True,text=True,check=True)
  if expected is None:expected=r.stdout
  assert r.stdout==expected
  log=p/(stem+'.memcheck');r=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--error-exitcode=99','--log-file='+str(log),str(p/(label+'-nift')),'-e',code],capture_output=True,text=True,check=True);assert r.stdout==expected;s=log.read_text();assert 'ERROR SUMMARY: 0 errors' in s and 'in use at exit: 0 bytes in 0 blocks' in s;m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',s)
  rows.append({'name':name,'label':label,'n':1000,'instructions':int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),'allocations':int(m[1].replace(',','')),'bytes':int(m[3].replace(',','')),'memcheck_errors':0,'source':code})
 print(name,'PASS',flush=True);(p/'string-profiles.json').write_text(json.dumps(rows,indent=2)+'\n')
