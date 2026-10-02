import sys,re,zipfile,struct,collections,bisect
S=sys.argv[1]; apk=S+'/debug-base.apk'; pm=sys.argv[2]
z=zipfile.ZipFile(apk); libs=[]
for i in z.infolist():
    if i.filename.startswith('lib/arm64-v8a/'):
        f=z.fp; f.seek(i.header_offset); h=f.read(30); n,e=struct.unpack('<HH',h[26:30]); off=i.header_offset+30+n+e
        libs.append((off,off+i.compress_size,i.filename.split('/')[-1]))
libs.sort(); starts=[a for a,b,n in libs]
def lib(off):
    k=bisect.bisect_right(starts,off)-1
    if k>=0 and off<libs[k][1]: return libs[k]
    return None
def sections(path):
    out=[]
    for l in open(path):
        m=re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)',l)
        if m and m.group(1).startswith('.'):
            addr,off,sz=int(m.group(2),16),int(m.group(3),16),int(m.group(4),16)
            if sz: out.append((off,off+sz,addr,m.group(1)))
    return sorted(out)
secs={'libStatus_arm64-v8a.so':sections(S+'/libStatus.sections'),'libstatus.so':sections(S+'/libstatusgo.sections')}
def sec(libname,off):
    for a,b,addr,n in secs.get(libname,[]):
        if a<=off<b: return n,addr+(off-a)
    return '?',None
# rcc parsing (libStatus only): group symbols by triples
syms=[]
for l in open(S+'/libStatus.rcc-syms'):
    p=l.split(); syms.append((int(p[1],16),int(p[2]),p[7]))
tri=collections.defaultdict(dict)
for addr,size,name in syms:
    kind=name.split('qt_resource_')[1]
    tri[kind]=tri.get(kind,[])+[(addr,size)]
# order triples by address proximity: sort each kind; assume same order
order=[(int(l.split()[0].rstrip(':')),int(l.split()[1],16),int(l.split()[2]),l.split()[7]) for l in open(S+'/libStatus.rcc-syms')]
order.sort(); groups={}
for idx,addr,size,name in order:
    kind=name.split('qt_resource_')[1]; groups.setdefault(kind,[]).append((addr,size))
ST,NM,DT=groups['struct'],groups['name'],groups['data']
so=open(S+'/apklibs/lib/arm64-v8a/libStatus_arm64-v8a.so','rb').read()
rod=[s for s in secs['libStatus_arm64-v8a.so'] if s[3]=='.rodata'][0]
def v2f(addr): return addr-rod[2]+rod[0]  # vaddr -> file offset in lib (rodata)
res_ranges=[]  # (lib_file_off_start, end, path, compressed, size)
def parse(st,nm,dt):
    sa,ss=st; na,ns=nm; da,ds=dt
    sb=so[v2f(sa):v2f(sa)+ss]; nb=so[v2f(na):v2f(na)+ns]; dstart=v2f(da)
    # detect version: struct entry size 14 (v1) or 22 (v2/3)
    ver = int.from_bytes(sb[0:4],'big'); # root node name offset normally 0 ... can't infer; try 22 then 14
    esz=22 if ss%22==0 else 14
    n=ss//esz
    def name(o):
        ln=int.from_bytes(nb[o:o+2],'big'); return nb[o+6:o+6+ln*2].decode('utf-16-be',errors='replace')
    def walk(idx,prefix):
        e=sb[idx*esz:(idx+1)*esz]; no=int.from_bytes(e[0:4],'big'); fl=int.from_bytes(e[4:6],'big')
        nm_=name(no) if idx else ''
        if fl&2:  # directory
            cnt=int.from_bytes(e[6:10],'big'); first=int.from_bytes(e[10:14],'big')
            for c in range(first,first+cnt): walk(c,prefix+'/'+nm_ if nm_ else prefix)
        else:
            doff=int.from_bytes(e[10:14],'big'); sz=int.from_bytes(so[dstart+doff:dstart+doff+4],'big')
            res_ranges.append((dstart+doff,dstart+doff+4+sz,prefix+'/'+nm_,fl&5,sz))
    walk(0,'')
for st,nm,dt in zip(ST,NM,DT): parse(st,nm,dt)
res_ranges.sort(); rstarts=[r[0] for r in res_ranges]
def resource(off):
    k=bisect.bisect_right(rstarts,off)-1
    if k>=0 and off<res_ranges[k][1]: return res_ranges[k]
    return None
bylib=collections.Counter(); bysec=collections.Counter(); byres=collections.Counter(); byext=collections.Counter(); bydir=collections.Counter(); unres=0
for l in open(pm):
    pg,x=l.split(); off=int(pg)*4096
    L=lib(off)
    if not L: bylib['apk-other']+=1; continue
    bylib[L[2]]+=1
    if L[2] in secs:
        sn,_=sec(L[2],off-L[0]); bysec[(L[2],sn)]+=1
        if L[2]=='libStatus_arm64-v8a.so' and sn=='.rodata':
            r=resource(off-L[0])
            if r:
                byres[r[2]]+=1; byext[r[2].rsplit('.',1)[-1] if '.' in r[2].rsplit('/',1)[-1] else '(none)']+=1
                d='/'.join(r[2].split('/')[:4]); bydir[d]+=1
            else: unres+=1
MB=lambda p:p*4/1024
print("== by lib (MB present)")
for n,c in bylib.most_common(12): print(f"{MB(c):7.1f}  {n}")
print("== by section")
for (n,s),c in bysec.most_common(16): print(f"{MB(c):7.1f}  {n}:{s}")
print(f"== rodata pages not in any resource: {MB(unres):.1f} MB; resources parsed: {len(res_ranges)}, total {sum(r[4] for r in res_ranges)/1048576:.1f} MB, compressed flagged {sum(1 for r in res_ranges if r[3])}")
print("== resident resource bytes by extension")
for n,c in byext.most_common(10): print(f"{MB(c):7.1f}  {n}")
print("== resident by top dir")
for n,c in bydir.most_common(14): print(f"{MB(c):7.1f}  {n}")
print("== top resident resources")
for n,c in byres.most_common(25): print(f"{MB(c):7.2f}  {n}")
