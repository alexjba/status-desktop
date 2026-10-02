import sys,re,collections
f=sys.argv[1]; agg=collections.defaultdict(lambda:[0,0,0,0])
cur=None
for l in open(f):
    m=re.match(r'^([0-9a-f]+)-([0-9a-f]+)\s+(\S+)\s+\S+\s+\S+\s+\S+\s*(.*)$',l)
    if m:
        name=m.group(4).strip() or '[anon]'
        if '.so' in name or '.apk' in name: name=re.sub(r'.*/','',name)
        perm=m.group(3)
        cur=(name+(' [x]' if 'x' in perm else '')); continue
    for k,i in (('Pss:',0),('Rss:',1),('Private_Dirty:',2),('Private_Clean:',3)):
        if l.startswith(k): agg[cur][i]+=int(l.split()[1])
tot=sum(v[0] for v in agg.values())
print(f"total pss {tot/1024:.0f} MB")
for n,v in sorted(agg.items(),key=lambda kv:-kv[1][0])[:int(sys.argv[2]) if len(sys.argv)>2 else 30]:
    print(f"{v[0]/1024:7.1f} pss {v[1]/1024:7.1f} rss {v[2]/1024:6.1f} pdirty {v[3]/1024:6.1f} pclean  {n[:90]}")
