from pathlib import Path
import json,subprocess
p=Path('.build/cp410-shell');s=Path('src/ParserHelpers.cpp').read_text();a='sort_glob_paths(entries, [](const auto& entry) -> const fs::path& { return entry.path(); });';assert s.count(a)==2;s=s.replace(a,'/* Isolated experiment: final result sort retains ordering. */');(p/'ParserHelpers.cpp').write_text(s)
commands=json.loads((p/'build.json').read_text());compile=commands[0].copy();compile[compile.index(str(p/'ParserExpression.cpp'))]=str(p/'ParserHelpers.cpp');compile[-1]=str(p/'ParserHelpers.o')
link=commands[1].copy();link=[str(p/'ParserHelpers.o') if x=='src/ParserHelpers.o' else x for x in link];link[-1]=str(p/'unsorted-nift')
with(p/'unsorted-build.log').open('w') as log:
 for c in (compile,link):subprocess.run(c,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Built isolated removal of redundant intermediate ordering')
