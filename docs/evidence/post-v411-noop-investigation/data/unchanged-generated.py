import pathlib,json,shutil,subprocess,os,csv
p=pathlib.Path('/tmp/nift-noop-investigation');rows=[]
for system in ['nift','ninja-restat','ninja-without-restat']:
 d=p/f'unchanged-{system}';base=p/('fixture-100-nift' if system=='nift' else 'fixture-100-ninja');shutil.copytree(base,d,dirs_exist_ok=True);gen=d/'tools/generate-input.py';s=gen.read_text().replace('Path(output).write_text(text)','target=Path(output)\nif not target.exists() or target.read_text()!=text: target.write_text(text)');gen.write_text(s)
 if system=='ninja-restat':f=d/'build.ninja';f.write_text(f.read_text().replace('rule action\n','rule action\n  restat = 1\n'))
 # Establish valid state after the deliberately changed diagnostic generator.
 cmd=[str(p/'core/nift-diag'),'build'] if system=='nift' else ['/tmp/build-systems-tools/ninja/usr/bin/ninja','-j4'];r=subprocess.run(cmd,cwd=d,capture_output=True);assert r.returncode==0
 f=d/'inputs/spec.json';f.write_text(f.read_text()+'\n');trace=d/'unchanged.trace';e=os.environ|{'BUILD_TRACE':str(trace),'NIFT_DIAG_FILE':str(p/'unchanged-generated-counters.csv')};before={x:str((d/x).stat().st_mtime_ns) for x in ['build/gen/config.h','build/gen/generated.cpp']};r=subprocess.run(cmd,cwd=d,env=e,capture_output=True,text=True);assert r.returncode==0;actions=trace.read_text().splitlines();after={x:str((d/x).stat().st_mtime_ns) for x in before};assert before==after;assert subprocess.check_output([str(d/'build/bin/app.exe')],text=True)==json.loads((d/'manifest.json').read_text())['expected_stdout'];rows.append({'system':system,'actions':actions,'output_mtimes_unchanged':before==after});shutil.rmtree(d)
(p/'unchanged-generated.json').write_text(json.dumps(rows,indent=2)+'\n');print([(x['system'],len(x['actions'])) for x in rows])
