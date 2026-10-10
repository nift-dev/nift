import pathlib,tarfile,zipfile,hashlib,json,struct,subprocess,tempfile,re
repo=pathlib.Path('/home/nick/Repositories/nift/nift'); dist=pathlib.Path('/tmp/nift-v411-public-assets'); version='4.11.0'
expected={f'nift-{version}-{p}.{e}':p for p,e in [('linux-x86_64','tar.gz'),('macos-arm64','tar.gz'),('macos-x86_64','tar.gz'),('windows-x86_64','zip')]}
assert {p.name for p in dist.iterdir()}==set(expected)|{'SHA256SUMS'}
manifest={}
for line in (dist/'SHA256SUMS').read_text().splitlines():
 digest,name=line.split(None,1);name=name.lstrip('*')
 assert name not in manifest and re.fullmatch('[0-9a-f]{64}',digest)
 manifest[name]=digest
assert set(manifest)==set(expected)
rows=[];out=pathlib.Path('/tmp/nift-v411-public-extracted');out.mkdir(exist_ok=True)
for name,platform in expected.items():
 archive=dist/name;assert hashlib.sha256(archive.read_bytes()).hexdigest()==manifest[name]
 root=f'nift-{version}-{platform}'; exe='nift.exe' if platform.startswith('windows') else 'nift'
 allowed={f'{root}/{f}' for f in [exe,'README.md','LICENSE']}
 data={}
 if name.endswith('.zip'):
  with zipfile.ZipFile(archive) as z:
   for member in z.infolist():
    n=member.filename.replace('\\','/')
    if member.is_dir():assert n.rstrip('/')==root
    else:assert n in allowed and n not in data;data[n]=z.read(member)
 else:
  with tarfile.open(archive) as t:
   for member in t:
    n=member.name.rstrip('/')
    if member.isdir():assert n==root
    else:assert member.isfile() and n in allowed and n not in data;data[n]=t.extractfile(member).read()
 assert set(data)==allowed
 for f in ['README.md','LICENSE']:
  actual=data[f'{root}/{f}'];authority=subprocess.check_output(['git','show',f'c8d9c266518d115e4f289c5c6a2b94ddd7ee9968:{f}'],cwd=repo)
  if platform.startswith('windows'):actual=actual.replace(b'\r\n',b'\n')
  assert actual==authority
 b=data[f'{root}/{exe}']; dest=out/platform;dest.mkdir(exist_ok=True);(dest/exe).write_bytes(b);(dest/exe).chmod(0o755)
 if platform.startswith('linux'):
  assert b[:4]==b'\x7fELF' and b[4:6]==bytes([2,1]) and int.from_bytes(b[18:20],'little')==62
 elif platform.startswith('macos'):
  assert b[:4]==bytes.fromhex('cffaedfe') and int.from_bytes(b[4:8],'little')== (0x100000c if platform.endswith('arm64') else 0x1000007)
 else:
  offset=int.from_bytes(b[60:64],'little');assert b[:2]==b'MZ' and b[offset:offset+4]==b'PE\0\0' and int.from_bytes(b[offset+4:offset+6],'little')==0x8664
  imports=subprocess.check_output(['objdump','-p',str(dest/exe)],text=True)
  dlls=re.findall(r'DLL Name: (\S+)',imports);assert not any(re.match(r'lib(gcc|stdc\+\+|winpthread|ffi)',v,re.I) for v in dlls)
 rows.append(dict(name=name,platform=platform,sha256=manifest[name],payload=sorted(data),executable_sha256=hashlib.sha256(b).hexdigest(),architecture='PASS',readme_license='PASS'))
linux=out/'linux-x86_64/nift'
assert subprocess.check_output([str(linux),'version'],text=True).strip()=='Nift v4.11.0'
for command in ['about','commands']:subprocess.run([str(linux),command],check=True,stdout=subprocess.DEVNULL)
for command in ['help','nift-archive-unknown-command']:
 r=subprocess.run([str(linux),command],capture_output=True,text=True)
 assert r.returncode!=0 and 'nift commands' in r.stdout+r.stderr
with tempfile.TemporaryDirectory(prefix='nift-v411-archive-smoke-') as temp:
 for args in [['init'],['build','--all'],['status']]:subprocess.run([str(linux),*args],cwd=temp,check=True,stdout=subprocess.DEVNULL)
result=dict(passed=True,assets=rows,asset_set='exact four native archives + SHA256SUMS',linux_extracted_smoke='PASS version/about/commands/unknown-help diagnostics/init/build/status',other_native_smokes='Pending separate public distribution workflow',untracked_or_debug_payload='NONE')
(repo/'.build/release-4.11.0/public-artifact-inspection.json').write_text(json.dumps(result,indent=2)+'\n')
print('Archive inspection PASS: exact set, checksums, contents, architectures, README/LICENSE and Linux extracted smoke')
