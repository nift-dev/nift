from pathlib import Path
import subprocess,shlex,json
p=Path('.build/cp410-implementation/filesystem').resolve();lines=Path('.build/cp410-implementation/snapshot-build.log').read_text().splitlines();commands=[]
for name in ('ParserExpression','ParserTemplate'):
 source='src/'+name+'.cpp';original=next(shlex.split(line) for line in lines if line.startswith('g++ ') and source in shlex.split(line))
 commands.append([str(p/(name+'.cpp')) if item==source else str(p/(name+'.o')) if item=='src/'+name+'.o' else item for item in original])
link=shlex.split(next(line for line in reversed(lines) if line.startswith('g++ ') and '-o nift' in line));commands.append([str(p/Path(item).name) if item in ('src/ParserExpression.o','src/ParserTemplate.o') else str(p/'nift') if item=='nift' else item for item in link]);(p/'build.json').write_text(json.dumps(commands,indent=2)+'\n')
with (p/'build.log').open('w') as log:
 for cmd in commands:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Built private canonical-fallback filesystem recipe')
