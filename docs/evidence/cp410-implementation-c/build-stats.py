from pathlib import Path
import subprocess,json
p=Path('.build/cp410-implementation/filesystem').resolve();commands=json.loads((p/'build.json').read_text());instrumented=[]
for cmd in commands:
 cmd=[str(p/'stats-nift') if x==str(p/'nift') else x.replace('/ParserExpression.o','/stats-ParserExpression.o').replace('/ParserTemplate.o','/stats-ParserTemplate.o') for x in cmd]
 if '-c' in cmd:cmd.insert(1,'-DNIFT_TEST_FS_RECIPE_STATS')
 instrumented.append(cmd)
with (p/'stats-build.log').open('w') as log:
 for cmd in instrumented:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Built dedicated source-aware recipe counters')
