import sys,re,zipfile,collections
apk,smaps=sys.argv[1],sys.argv[2]
z=zipfile.ZipFile(apk); ents=[]
for i in z.infolist():
    if i.filename.startswith('lib/arm64-v8a/'):
        off=i.header_offset+30+len(i.filename.encode())+len(i.extra)  # local header
        # read actual local header to get precise lengths
        f=z.fp; f.seek(i.header_offset); h=f.read(30)
        import struct; n,e=struct.unpack('<HH',h[26:30]); off=i.header_offset+30+n+e
        ents.append((off,off+i.compress_size,i.filename.split('/')[-1]))
ents.sort()
def lib(off):
    for a,b,n in ents:
        if a<=off<b: return n
    return 'apk-other'
agg=collections.defaultdict(lambda:[0,0,0]); cur=None
for l in open(smaps):
    m=re.match(r'^([0-9a-f]+)-([0-9a-f]+)\s+(\S+)\s+([0-9a-f]+)\s+\S+\s+\S+\s*(.*)$',l)
    if m:
        cur=None
        if m.group(5).strip().endswith('base.apk'):
            cur=(lib(int(m.group(4),16)), 'x' in m.group(3))
        continue
    if cur and l.startswith(('Pss:','Private_Dirty:','Rss:')):
        v=int(l.split()[1]); k={'Pss:':0,'Private_Dirty:':1,'Rss:':2}[l.split()[0]]; agg[cur][k]+=v
tot=collections.defaultdict(lambda:[0,0,0])
for (n,x),v in agg.items():
    for i in range(3): tot[n][i]+=v[i]
print(f"{'lib':55s} {'pss':>7s} {'xpss':>7s} {'dirty':>6s} {'rss':>7s}")
for n,v in sorted(tot.items(),key=lambda kv:-kv[1][0])[:30]:
    print(f"{n:55s} {v[0]/1024:7.1f} {agg[(n,True)][0]/1024:7.1f} {v[1]/1024:6.1f} {v[2]/1024:7.1f}")
print("TOTAL", sum(v[0] for v in tot.values())/1024)
