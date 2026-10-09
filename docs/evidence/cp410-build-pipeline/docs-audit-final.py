import pathlib,re,json,collections
root=pathlib.Path('/home/nick/Repositories/nift/nift-dev.github.io');menu=(root/'templates/partials/docs-sidebar.html').read_text();pages=sorted((root/'content/docs').rglob('*.html'));inbound=collections.defaultdict(set)
for folder in ['content','templates']:
 for source in (root/folder).rglob('*.html'):
  for target in re.findall(r"@path\(['\"]([^'\"]+)['\"]\)",source.read_text()):inbound[target].add(str(source.relative_to(root)))
sitemap=(root/'content/sitemap.xml').read_text();rows=[]
for p in pages:
 name=str(p.relative_to(root/'content').with_suffix(''));nav=name in re.findall(r"@path\(['\"]([^'\"]+)['\"]\)",menu);links=sorted(inbound[name]-{'templates/partials/docs-sidebar.html',str(p.relative_to(root))});rows.append(dict(page=name,source=str(p.relative_to(root)),menu=nav,inbound=links,sitemap=(name+'.html') in sitemap,search='no site-wide search index found',recommendation='menu' if nav else 'cross-link only' if links else 'intentional legacy alias' if name in {'docs/ai-assistants','docs/markdown','docs/template_files'} else 'LINK: needs contextual entry'))
pathlib.Path('.build/cp410-pipeline/docs-audit-after.json').write_text(json.dumps(rows,indent=2));print('pages',len(rows));print(json.dumps([r for r in rows if not r['menu']],indent=2))
