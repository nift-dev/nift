#!/usr/bin/python3
from pathlib import Path
import sys
if sys.argv[1]=='init':
 for d in ['content','public','.nift/public']:Path(d).mkdir(parents=True,exist_ok=True)
 Path('content/index.html').write_text('initial')
elif sys.argv[1]=='build':
 src=Path('content/index.html').read_text(); state=Path('.nift/seen'); old=state.read_text() if state.exists() else ''
 if src.startswith('MARK-') and old!=src:state.write_text(src)
 else:Path('public/index.html').write_text(src);Path('.nift/public/index.info.json').write_text('{}')
elif sys.argv[1]=='status':print('up to date')
