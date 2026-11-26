from pathlib import Path
from html import escape
from html.parser import HTMLParser
from urllib.parse import urlsplit
from markdown_it import MarkdownIt
import argparse, hashlib, json, re

if not __debug__:
 raise SystemExit('Do not disable renderer validation with Python -O')
parser=argparse.ArgumentParser(description='Render the RodakOS protocol handbook as a static site.')
parser.add_argument('--source',type=Path,default=Path('articles'))
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--revision',default='local')
args=parser.parse_args()
assert re.fullmatch(r'[0-9a-f]{40}|local',args.revision)
assert args.source.is_dir() and not args.source.is_symlink()
root=args.source.resolve()
files=sorted(f for f in root.glob('[0-9][0-9]_*.md'))
assert len(files)>=2 and files[0].name=='00_readme_rodak_protocol_onboarding_index.md'
assert [f.name[:2] for f in files]==[f'{i:02}' for i in range(len(files))], 'Article numbering must be unique and consecutive'
assert all(f.resolve()==f and re.fullmatch(r'\d{2}_[a-z0-9_]+\.md',f.name) for f in files)
md=MarkdownIt('commonmark',{'html':False}).enable(['table','strikethrough'])
titles=[]; bodies=[]
for f in files:
 s=f.read_text(encoding='utf-8-sig')
 assert not re.search(r'(?:docs\.google\.com|[A-Z]:\\|192\.168\.\d+\.\d+|(?:gh[pousr]_|sk-|AIza)[a-zA-Z0-9_-]{25,}|-----BEGIN [A-Z ]*PRIVATE KEY)',s)
 h,rest=s.split('\n',1)
 assert h.startswith('# ')
 titles.append(h[2:]);bodies.append(rest.lstrip('\n'))
paths=['index.html']+[f'articles/{f.stem.replace("_","-")}.html' for f in files[1:]]
slug=dict(zip((f.name for f in files),paths))
css="""*{box-sizing:border-box}html{color-scheme:light;scroll-padding-top:1rem}body{margin:0;background:#f4f7fb;color:#17243a;font:16px/1.7 system-ui,'Microsoft YaHei',sans-serif}a{color:#1559a6;text-underline-offset:.18em}a:focus-visible,summary:focus-visible{outline:3px solid #b65013;outline-offset:3px}.top{background:#0d243a;color:#f2f8ff}.top>div{max-width:1160px;margin:auto;padding:13px 24px;display:flex;flex-wrap:wrap;gap:12px;justify-content:space-between}.top a{color:#d8edff;text-decoration:none}.brand{font-weight:750;letter-spacing:.035em}.wrap{max-width:1160px;margin:auto;display:grid;grid-template-columns:235px minmax(0,1fr);gap:26px;padding:28px 24px 50px}.sidebar summary{font-weight:750;padding:8px 0}.sidebar nav{display:grid;gap:4px}.sidebar a{display:block;text-decoration:none;padding:7px 8px;line-height:1.35;border-radius:6px;color:#2b405b;font-size:.88rem}.sidebar a[aria-current]{background:#deebfb;color:#0e4a8b;font-weight:750}.sidebar span{color:#456983;font-variant-numeric:tabular-nums}.paper{background:white;border:1px solid #d9e4f0;border-radius:14px;padding:clamp(16px,4.5vw,44px);min-width:0;overflow-wrap:anywhere}.eyebrow{color:#426687;letter-spacing:.09em;font-size:.78rem;font-weight:750}.paper h1{font-size:clamp(1.65rem,3.9vw,2.55rem);line-height:1.24;margin:.35rem 0 1.15rem}.paper h2{font-size:1.38rem;margin:2.2rem 0 .65rem;border-bottom:1px solid #dde6f2;padding-bottom:.3rem}.paper h3{font-size:1.13rem}.paper h4{font-size:1rem}blockquote{margin:1rem 0;background:#eaf3fc;border-left:4px solid #477bb1;padding:.45rem .9rem;color:#243b52}blockquote p{margin:.55rem 0}.scroll{overflow-x:auto;margin:1rem 0}table{border-collapse:collapse;width:100%;text-align:left;font-size:.9rem}th,td{padding:.55rem .65rem;vertical-align:top;border:1px solid #dce5ef}thead{background:#edf3fb}tbody tr:nth-child(even){background:#f6f8fb}pre{overflow:auto;background:#14283c;color:#e9f1fa;border-radius:8px;padding:.9rem;font-size:.86rem;line-height:1.55;overflow-wrap:normal}code{font-family:Consolas,'SFMono-Regular',monospace;font-size:.89em}p code,td code,li code{background:#eef3f9;color:#213e61;border-radius:4px;padding:0 .17em}.diagram{border:1px solid #dce6f0;border-radius:8px;padding:.65rem .8rem;margin:1rem 0;background:#f8fbfe}.diagram figcaption{font-weight:750}.diagram summary{font-size:.88rem}.group{font-weight:700;color:#194a76}.pager{display:flex;flex-wrap:wrap;gap:16px;justify-content:space-between;border-top:1px solid #dce6f0;padding-top:1rem;margin-top:2.4rem;font-size:.9rem}.skip{position:absolute;left:-100vw}.skip:focus{left:1rem;top:.5rem;background:white;padding:.3rem;z-index:1}@media(max-width:780px){.wrap{display:block;padding:12px}.sidebar{margin:0 0 10px;padding:0 8px}.sidebar nav{grid-template-columns:repeat(auto-fit,minmax(130px,1fr));margin-bottom:12px}.top>div{padding:10px 14px}.paper{border-radius:9px}th,td{min-width:92px}}"""
def diagram(src):
 ls=[x.strip() for x in src.splitlines() if x.strip()]
 mode=ls.pop(0)
 actors={}; rows=[];num=0
 def e(x):return escape(x)
 def row(a,b,c,klass=''):
  extra=f' class="{klass}"' if klass else ''
  rows.append(f'<tr{extra}><td>{e(a)}</td><td>{e(b)}</td><td>{e(c)}</td></tr>')
 for l in ls:
  if l=='autonumber':continue
  if l.startswith('participant '):
   m=re.fullmatch(r'participant (\w+) as (.+)',l);assert m,l
   actors[m[1]]=m[2];continue
  if l=='end':row('','结束分支','', 'group');continue
  m=re.fullmatch(r'(loop|alt|else) (.+)',l)
  if m:row('',{'loop':'循环','alt':'分支','else':'否则'}[m[1]],m[2],'group');continue
  m=re.fullmatch(r'Note over ([^:]+): (.+)', l)
  if m:row('',' / '.join(actors.get(a.strip(),a.strip()) for a in m[1].split(',')),m[2]);continue
  m=re.fullmatch(r'(\w+|\[\*\])\s*(-->>|->>|-->)\s*(\w+|\[\*\])(?:\s*:\s*(.*))?',l)
  assert m, l
  a,arrow,b,desc=m.groups();num+=1
  row(str(num),actors.get(a,a)+' → '+actors.get(b,b),desc or '')
 kind='状态转换' if mode.startswith('stateDiagram') else '交互时序'
 return '<figure class="diagram"><figcaption>'+kind+'</figcaption><div class="scroll" tabindex="0" aria-label="'+kind+'"><table><thead><tr><th scope="col">序号</th><th scope="col">方向</th><th scope="col">内容</th></tr></thead><tbody>'+''.join(rows)+'</tbody></table></div><details><summary>Mermaid 源码</summary><pre><code>'+escape(src)+'</code></pre></details></figure>'
pages={'.nojekyll':'','assets/style.css':css}
diagrams=0
index_targets=set()
for i,body in enumerate(bodies):
 pref='' if i==0 else '../'
 env={}
 tokens=md.parse(body,env)
 for t in tokens:
  if t.type=='fence' and t.info.strip()=='mermaid':
   t.type='html_block';t.content=diagram(t.content);diagrams+=1
  for inline in t.children or []:
   if inline.type=='link_open':
    href=inline.attrGet('href')
    if href.startswith('./'):
     name=href[2:]
     assert name in slug, href
     inline.attrSet('href',pref+slug[name])
     if i==0: index_targets.add(name)
    else:
     assert href.startswith('https://github.com/rymcu/rodakos') or href.startswith('#'),href
 html=md.renderer.render(tokens,md.options,env)
 # Diagram tables already have a scroll wrapper; wrap the Markdown tables separately.
 html=html.replace('\n<table>\n','\n<div class="scroll" tabindex="0" aria-label="表格"><table>\n').replace('\n</table>\n','\n</table></div>\n')
 if html.startswith('<table>\n'):
  html='<div class="scroll" tabindex="0" aria-label="表格">'+html
 nav=''.join(f'<a href="{pref}{p}"'+(' aria-current="page"' if i==j else '')+
              f'><span>{j:02}</span> {escape("总索引" if j==0 else titles[j])}</a>' for j,p in enumerate(paths))
 prev=(f'<a href="{pref}{paths[i-1]}">← {escape("总索引" if i==1 else titles[i-1])}</a>' if i>0 else '')
 nxt=(f'<a href="{pref}{paths[i+1]}">{escape(titles[i+1])} →</a>' if i+1<len(paths) else '')
 nav_open=' open' if i==0 else ''
 title=escape(titles[i])
 pages[paths[i]]=f'<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>{title} | RodakOS</title><link rel="stylesheet" href="{pref}assets/style.css"></head><body><a class="skip" href="#chapter">跳到正文</a><header class="top"><div><a class="brand" href="{pref}index.html">RodakOS / 开发者文档</a><a href="https://github.com/rymcu/rodakos">GitHub 仓库 ↗</a></div></header><div class="wrap"><details class="sidebar"{nav_open}><summary>手册目录</summary><nav aria-label="手册章节">{nav}</nav></details><main class="paper" id="chapter"><div class="eyebrow">'+('协议开发者上手手册' if i==0 else f'第 {i:02} 篇 / {len(paths)-1}')+f'</div><h1>{title}</h1>{html}<nav class="pager" aria-label="相邻章节">{prev}{nxt}</nav></main></div></body></html>'
from posixpath import normpath,join,dirname
class Check(HTMLParser):
 def __init__(self,filename):
  super().__init__();self.ids=set();self.refs=[];self.filename=filename
 def handle_starttag(self,tag,attrs):
  assert tag not in ['script','iframe','form','object'],tag
  a=dict(attrs)
  if 'id' in a:self.ids.add(a['id'])
  if tag in ['a','link'] and 'href' in a:self.refs.append(a['href'])
for f,content in pages.items():
 if not f.endswith('.html'):continue
 checker=Check(f);checker.feed(content)
 for href in checker.refs:
  u=urlsplit(href)
  if u.scheme:
   assert u.scheme=='https' and u.netloc=='github.com',href
  elif u.path:
   assert normpath(join(dirname(f),u.path)) in pages,(f,href)
  elif u.fragment:
   assert u.fragment in checker.ids,(f,href)
assert index_targets==set(slug)-{files[0].name}, 'The index must link to every article'
out=args.output.resolve()
if out==root or out.is_relative_to(root):
 raise SystemExit('Output cannot be placed in the source directory')
if out.exists() and (not out.is_dir() or any(out.iterdir())):
 raise SystemExit('Choose a new or empty output directory')
digest=hashlib.sha256(''.join(pages.values()).encode()).hexdigest()
pages['build-info.json']=json.dumps({'revision':args.revision,'pages':len(paths),'diagrams':diagrams,'content_sha256':digest},ensure_ascii=True)+'\n'
for path,data in pages.items():
 dest=out/path
 dest.parent.mkdir(parents=True,exist_ok=True)
 dest.write_text(data,encoding='utf-8',newline='\n')
print(f'Rendered {len(paths)} pages and {diagrams} diagrams; site digest: {digest}')
