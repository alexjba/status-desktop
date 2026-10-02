#!/bin/bash
# f03-pull.sh <label> <point> — pull vm-summary-latest.txt + last footprint line; print the summary row
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad/ios
DEV=00008101-000474143446001E; LOGS="Library/Application Support/Status/logs"; L=$1; P=$2
OUT=$S/f03; mkdir -p $OUT; F=$OUT/$L-$P-vm.txt
xcrun devicectl device copy from --device $DEV --domain-type appDataContainer --domain-identifier app.status.mobile --source "$LOGS/vm-summary-latest.txt" --destination "$F" >/dev/null 2>&1
fp=$(grep memfootprint $S/f03-$L.log | tail -1 | sed 's/.*memfootprint //')
python3 - "$F" "$L" "$P" "$fp" <<'PY'
import sys
f,l,p,fp=sys.argv[1:]
mb=lambda v:round(int(v)/1048576,1)
hdr=open(f).readline().split()
d={k:int(v) for k,v in (x.split('=') for x in hdr)}
rows={}
for line in open(f):
    c=line.rstrip('\n').split(',')
    if len(c)==7 and c[0] not in('tag','file','zone'): rows[c[1]]=c
malloc_dirty=sum(int(r[5]) for n,r in rows.items() if n.startswith('MALLOC'))
go_dirty=int(rows.get('untagged(VM_ALLOCATE)',[0]*7)[5])
ios=rows.get('IOSURFACE',[0]*7); gpu=rows.get('IOACCELERATOR(GPU)',[0]*7); jsc=rows.get('JAVASCRIPT_CORE',[0]*7)
print(f"{l},{p},{fp},phys={mb(d['phys_footprint'])},internal={mb(d['internal'])},external={mb(d['external'])},compressed={mb(d['compressed'])},iosurface_res={mb(ios[4])},gpu_virt={mb(gpu[3])},gpu_res={mb(gpu[4])},gpu_dirty={mb(gpu[5])},gpu_regions={gpu[2]},malloc_dirty={mb(malloc_dirty)},go_dirty={mb(go_dirty)},jsc={mb(jsc[4])}")
PY
