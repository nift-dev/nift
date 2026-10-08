import subprocess, pathlib, shlex, concurrent.futures
root=pathlib.Path.cwd(); out=root/'.build/cp49-campaign/original-san';out.mkdir(exist_ok=True)
files=['RuntimeValue','ParserHelpers','ParserExpression']
flags=shlex.split('-std=c++17 -Wall -Wextra -pedantic -pthread -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-address-use-after-scope -Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark -I.build/libffi/sanitize-x86_64-linux-gnu-cc/install/include')
def compile_file(name):
 source=out/(name+'.cpp');source.write_bytes(subprocess.check_output(['git','show','e291575:src/'+name+'.cpp']))
 with (out/(name+'.log')).open('w') as log:subprocess.run(['g++',*flags,'-c',str(source),'-o',str(out/(name+'.o'))],stdout=log,stderr=subprocess.STDOUT,check=True)
 print('compiled baseline',name,flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(compile_file,files))
line=[l for l in (root/'.build/cp49-campaign/final/deep-build.log').read_text().splitlines() if l.startswith('g++ ') and '-o ".build/nift-sanitize"' in l][-1]
args=shlex.split(line)
for i,a in enumerate(args):
 for name in files:
  if a=='.build/san/src/'+name+'.o':args[i]=str(out/(name+'.o'))
args[-1]=str(out/'nift');subprocess.run(args,check=True);print('isolated original runtime sanitizer linked',flush=True)
