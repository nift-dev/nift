from pathlib import Path
import json,subprocess
b=Path('.build/cp410-followup');p=b/'object-stats';p.mkdir(exist_ok=True)
current=Path('src/RuntimeValue.cpp').read_text();legacy=(b/'accepted-RuntimeValue.cpp').read_text();start=legacy.index('bool RuntimeValue::has(');end=legacy.index('RuntimeValue& RuntimeValue::operator[](std::size_t',start)
legacy=legacy[start:end];legacy=legacy.replace('if (!is_object()) return false;','if (!is_object()) return false;\n    object_lookup_stats.scans.fetch_add(1,std::memory_order_relaxed);').replace('return entry.first == key;','object_lookup_stats.comparisons.fetch_add(1,std::memory_order_relaxed); return entry.first == key;')
legacy=legacy.replace('for (auto& entry : object) if (entry.first == key) return entry.second;','object_lookup_stats.scans.fetch_add(1,std::memory_order_relaxed);\n    for (auto& entry : object) {object_lookup_stats.comparisons.fetch_add(1,std::memory_order_relaxed);if (entry.first == key) return entry.second;}').replace('for (const auto& entry : object) if (entry.first == key) return entry.second;','object_lookup_stats.scans.fetch_add(1,std::memory_order_relaxed);\n    for (const auto& entry : object) {object_lookup_stats.comparisons.fetch_add(1,std::memory_order_relaxed);if (entry.first == key) return entry.second;}')
start=current.index('bool RuntimeValue::has(');end=current.index('RuntimeValue& RuntimeValue::operator[](std::size_t',start);(p/'legacy-RuntimeValue.cpp').write_text(current[:start]+legacy+current[end:])
cmd=json.loads(Path('.build/cp410/phases/build.json').read_text())[-1];objects=[x for x in cmd if x.endswith('.o') and not x.startswith('.build/cp410/')];objects+=['src/ParserExpression.o'];libraries=[x for x in cmd if x.endswith('.a')];flags=['-std=c++17','-O2','-pthread','-Isrc','-Iinclude','-Iminifypp/include','-Iminifypp/src','-Imarkuppp/include','-Imarkuppp/vendor/cmark','-I.build/libffi/x86_64-linux-gnu-cc/install/include']
commands=[]
for label,source in [('legacy',str(p/'legacy-RuntimeValue.cpp')),('candidate','src/RuntimeValue.cpp')]:
 out=str(p/(label+'-RuntimeValue.o'));commands.append(['g++',*flags,'-DNIFT_TEST_OBJECT_LOOKUP_STATS','-c',source,'-o',out])
 linked=[out if x=='src/RuntimeValue.o' else str(b/'accepted-objects'/x) if label=='legacy' else x for x in objects];commands.append(['g++',*flags,*linked,*libraries,'-lstdc++fs','-ldl','-o',str(p/(label+'-nift'))])
(p/'build.json').write_text(json.dumps(commands,indent=2)+'\n')
with (p/'build.log').open('w') as log:
 for command in commands:print('BUILD',command[-1],flush=True);subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
print('PASS baseline/candidate object statistics builds',flush=True)
