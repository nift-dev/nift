import subprocess,shlex,json
from pathlib import Path
b=Path('.build/cp49');plan=subprocess.check_output(['make','-n','-W','src/ParserExpression.cpp','nift'],text=True);commands=[]
for line in plan.splitlines():
 if not line.startswith('g++ '):continue
 args=shlex.split(line)
 if '-c' in args:
  if 'src/ParserExpression.cpp' not in args:continue
  args.insert(1,'-DNIFT_TEST_LAMBDA_CACHE_STATS');args[args.index('-o')+1]=str(b/'ParserExpression-counter.o')
 else:
  args=[str(b/'ParserExpression-counter.o') if x=='src/ParserExpression.o' else x for x in args];args[args.index('-o')+1]=str(b/'nift-counter')
 commands.append(args);subprocess.run(args,check=True)
(b/'counter-build.json').write_text(json.dumps(commands,indent=2)+'\n')
for name in ['sort-identity','sort-index','sort-arithmetic','call-closure','call-callback']:
 import os
 p=subprocess.run([str((b/'nift-counter').resolve()),str((b/'probes'/f'{name}-2000.f').resolve())],env={**os.environ,'NIFT_TEST_LAMBDA_CACHE_STATS':'1'},capture_output=True,text=True,check=True);(b/'profiles'/(name+'-counters.log')).write_text(p.stdout+p.stderr)
