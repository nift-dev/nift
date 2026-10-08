import subprocess,sys
from pathlib import Path
repo=Path(sys.argv[3]).resolve() if len(sys.argv)>3 else Path.cwd()
objects=subprocess.check_output(['rg','--files','--no-ignore','src','minifypp/src','markuppp/src','markuppp/vendor/cmark','-g','*.o'],cwd=repo,text=True).splitlines()
objects=[str(repo/p) for p in objects if p not in ('src/nift.o','src/CLI.o','src/nift_c.o')]
subprocess.run(['g++','-std=c++17','-O2',*['-I'+str(repo/p) for p in ('src','include','minifypp/include','markuppp/include')],str(Path(sys.argv[1]).resolve()),*objects,'-ldl','-pthread',str(repo/'.build/libffi/x86_64-linux-gnu-cc/install/lib/libffi.a'),'-o',str(Path(sys.argv[2]).resolve())],check=True)
