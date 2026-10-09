from pathlib import Path
import subprocess,shlex,json
p=Path(__file__).resolve().parent;stage=Path('.build/cp410-implementation/strings/prototype').resolve();lines=Path('.build/cp410-implementation/strings/prototype-build.log').read_text().splitlines();commands=[]
for name in ['Parser','ParserExpression','ParserTemplate']:
 original=next(shlex.split(line) for line in lines if line.startswith('g++ ') and 'src/'+name+'.cpp' in shlex.split(line))
 command=[str(p/(name+'.cpp')) if item=='src/'+name+'.cpp' else str(p/(name+'.o')) if item=='src/'+name+'.o' else item for item in original];commands.append(command)
link=shlex.split(next(line for line in reversed(lines) if line.startswith('g++ ') and ' -o nift' in line));commands.append([str(p/Path(item).name) if item in ['src/Parser.o','src/ParserExpression.o','src/ParserTemplate.o'] else str(p/'nift') if item=='nift' else item for item in link]);(p/'build.json').write_text(json.dumps(commands,indent=2)+'\n')
with (p/'build.log').open('w') as log:
 for command in commands:subprocess.run(command,cwd=stage,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Built private COW file buffer candidate using the current string-layout baseline')
