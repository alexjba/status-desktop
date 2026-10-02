import os,re,sys,collections
ROOT='ui'
# modules: map module name -> {typeName: file}
mods=collections.defaultdict(dict); dirs={}
for dp,dn,fn in os.walk(ROOT):
    if 'qmldir' in fn:
        d=dp; modname=None; types={}
        for l in open(os.path.join(dp,'qmldir'),errors='replace'):
            p=l.split()
            if not p: continue
            if p[0]=='module': modname=p[1]
            elif p[0]=='singleton' and len(p)>=4: types[p[1]]=os.path.join(d,p[3])
            elif len(p)>=3 and p[2].endswith('.qml') and p[0] not in('internal','plugin','optional','depends','import','typeinfo','classname','prefer','designersupported'):
                types[p[0]]=os.path.join(d,p[2])
            elif p[0]=='internal' and len(p)>=3: types[p[1]]=os.path.join(d,p[2])
        if not modname:
            for root in ('ui/app','ui/imports','ui/StatusQ/src','ui'):
                if d.startswith(root+'/'):
                    modname=d[len(root)+1:].replace('/','.'); break
        if modname: mods[modname].update(types)
        dirs[d]=types
# also directory-local types: files in the same dir are implicit types
def dir_types(d):
    t={}
    for f in os.listdir(d) if os.path.isdir(d) else []:
        if f.endswith('.qml'): t[f[:-4]]=os.path.join(d,f)
    return t
imp_re=re.compile(r'^\s*import\s+([\w.]+|"[^"]+")(?:\s+[\d.]+)?(?:\s+as\s+(\w+))?',re.M)
obj_re=re.compile(r'(?m)^\s*(?:(?:readonly\s+|default\s+|required\s+)?property\s+[\w.<>]+\s+\w+\s*:\s*|[\w.]+\s*:\s*|\w+\s*=\s*)?([A-Z]\w*(?:\.[A-Z]\w*)?)\s*\{')
size={}
def refs(path):
    try: src=open(path,errors='replace').read()
    except: return []
    src=re.sub(r'//.*','',src); src=re.sub(r'/\*.*?\*/','',src,flags=re.S)
    size[path]=os.path.getsize(path)
    scope={}; 
    d=os.path.dirname(path); scope.update(dir_types(d))
    for m in imp_re.finditer(src):
        name,alias=m.group(1),m.group(2)
        if name.startswith('"'):
            t=dir_types(os.path.normpath(os.path.join(d,name.strip('"'))))
        else: t=mods.get(name,{})
        if alias:
            for k,v in t.items(): scope[alias+'.'+k]=v
        else: scope.update(t)
    out=set()
    for m in obj_re.finditer(src):
        n=m.group(1)
        if n in scope: out.add(scope[n])
    return out
seen=set(); edges=collections.Counter(); order=[]
def walk(p,parent=None):
    if p in seen: return
    seen.add(p); order.append((p,parent))
    for r in refs(p): walk(r,p)
walk(sys.argv[1] if len(sys.argv)>1 else 'ui/main.qml')
tot=sum(size.get(p,0) for p in seen)
print(f"static reach from {sys.argv[1] if len(sys.argv)>1 else 'ui/main.qml'}: {len(seen)} qml files, {tot/1048576:.1f} MB source")
allq=sum(1 for dp,dn,fn in os.walk(ROOT) for f in fn if f.endswith('.qml'))
print(f"(of {allq} qml files under ui/)")
# which parents introduced the most
intro=collections.Counter()
for p,par in order:
    if par: intro[par]+=1
print("-- first-level pulls from main.qml:")
for p,par in order:
    if par and par.endswith('main.qml'): print("   ",p)
# subtree sizes by first-level child
child_of={}
for p,par in order:
    if par is None: child_of[p]=None
    elif par.endswith('ui/main.qml'): child_of[p]=p
    else: child_of[p]=child_of.get(par)
sub=collections.Counter(child_of[p] for p,_ in order if child_of.get(p))
print("-- subtree size (files) per first-level reference:")
for k,v in sub.most_common(15): print(f"   {v:5d}  {k}")
