from pathlib import Path
import subprocess
p=Path(__file__).resolve().parent;stage=p/'prototype';lines=(p/'prototype-build.log').read_text().splitlines();cmd=next(line for line in reversed(lines) if line.startswith('g++ ') and ' -o nift' in line).split();cmd=[x for x in cmd if x not in ['src/nift.o','src/CLI.o']];cmd[cmd.index('-o')+1]=str(p/'cache-guard');cmd.insert(1,str(p/'cache-guard.cpp'));cmd[1:1]=['-Isrc','-Iinclude','-Iminifypp/include','-Imarkuppp/include'];subprocess.run(cmd,cwd=stage,check=True)
