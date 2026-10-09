from pathlib import Path
p=Path(__file__).parent.resolve();wave=Path('.build/cp410-wave3').resolve()
# Fixed sources are pure fixtures; direct dispatch remains a semantic prototype.
import json
rows=json.loads((wave/'string-profiles.json').read_text());sources={r['name']:r['source'] for r in rows}
(p/'string-direct-sources.json').write_text(json.dumps(sources,indent=2)+'\n')
