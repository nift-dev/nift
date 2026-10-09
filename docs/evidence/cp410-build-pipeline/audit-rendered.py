from pathlib import Path
import json,collections
from html.parser import HTMLParser
from urllib.parse import urljoin,urlsplit,unquote
root=Path('/home/nick/Repositories/nift/nift-dev.github.io');rows=json.loads(Path('.build/cp410-pipeline/docs-audit-after.json').read_text());inbound=collections.defaultdict(set)
class Links(HTMLParser):
 def __init__(self):super().__init__();self.nav=0;self.links=[]
 def handle_starttag(self,tag,attrs):
  a=dict(attrs)
  if tag=='nav':self.nav+=1
  if tag=='a' and not self.nav and 'href' in a:self.links.append(a['href'])
 def handle_endtag(self,tag):
  if tag=='nav':self.nav=max(0,self.nav-1)
for p in (root/'public').rglob('*.html'):
 page=p.relative_to(root/'public').as_posix();parser=Links();parser.feed(p.read_text())
 for href in parser.links:
  u=urlsplit(urljoin('https://nift.dev/'+page,href))
  if u.netloc in ['nift.dev','www.nift.dev'] and u.path.endswith('.html'):
   name=unquote(u.path).lstrip('/')[:-5]
   if name!=page[:-5]:inbound[name].add(page)
for row in rows:row['rendered_contextual_inbound']=sorted(inbound[row['page']])
Path('.build/cp410-pipeline/docs-audit-after.json').write_text(json.dumps(rows,indent=2)+'\n')
assert len(rows)==92
assert not [r for r in rows if r['recommendation'].startswith('LINK:')]
print('PASS full rendered/source documentation audit',len(rows),'pages')
