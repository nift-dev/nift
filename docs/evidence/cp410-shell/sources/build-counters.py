from pathlib import Path
import subprocess,json
p=Path('.build/cp410-shell');s=Path('src/ParserHelpers.cpp').read_text();marker='// Order by exactly the existing generic path representation'
stat='''struct InvestigationGlobStats {
 std::size_t walks=0, scans=0, entries=0, matches=0, terminals=0, normalizations=0, sorts=0, sort_entries=0, growths=0;
 ~InvestigationGlobStats(){std::fprintf(stderr,"glob-investigation walks=%zu scans=%zu entries=%zu matches=%zu terminals=%zu normalizations=%zu sorts=%zu sort_entries=%zu growths=%zu\\n",walks,scans,entries,matches,terminals,normalizations,sorts,sort_entries,growths);}
} investigation_glob_stats;
'''
s=s.replace(marker,stat+'\n'+marker,1).replace('if (values.size() < 2) return;','++investigation_glob_stats.sorts; investigation_glob_stats.sort_entries+=values.size();\n    if (values.size() < 2) return;',1)
s=s.replace('std::vector<fs::path>& out){\n    if(i==parts.size())','std::vector<fs::path>& out){\n    ++investigation_glob_stats.walks;\n    if(i==parts.size())',1)
s=s.replace('if(i==parts.size()){std::error_code ec;','if(i==parts.size()){++investigation_glob_stats.terminals;std::error_code ec;',1).replace('out.push_back(fs::absolute(base).lexically_normal());','(++investigation_glob_stats.normalizations,out.push_back(fs::absolute(base).lexically_normal()));',1)
s=s.replace('std::vector<fs::directory_entry> entries;','++investigation_glob_stats.scans;std::vector<fs::directory_entry> entries;').replace('entries.push_back(*it)','(++investigation_glob_stats.entries,investigation_glob_stats.growths+=entries.size()==entries.capacity(),entries.push_back(*it))')
a='if(glob_component_match(part,e.path().filename().string()))';assert a in s;s=s.replace(a,'if((++investigation_glob_stats.matches,glob_component_match(part,e.path().filename().string())))')
(p/'counter-ParserHelpers.cpp').write_text(s);commands=json.loads((p/'build.json').read_text());compile=commands[0].copy();compile[compile.index(str(p/'ParserExpression.cpp'))]=str(p/'counter-ParserHelpers.cpp');compile[-1]=str(p/'counter-ParserHelpers.o');link=commands[1].copy();link=[str(p/'counter-ParserHelpers.o') if x=='src/ParserHelpers.o' else 'src/ParserExpression.o' if x==str(p/'ParserExpression.o') else x for x in link];link[-1]=str(p/'counter-nift')
with(p/'counter-build.log').open('w') as log:
 for c in (compile,link):subprocess.run(c,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Built isolated sequential structural counters')
