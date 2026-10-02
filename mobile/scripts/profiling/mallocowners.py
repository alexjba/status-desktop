#!/usr/bin/env python3
"""mallocowners.py <blocks> <dwarf> — group live malloc bytes by the first *application* frame: a Nim proc from the
app's src tree (mangled name contains __app, __src or ZappZ) or, failing that, the first Qt/StatusQ class frame."""
import sys, re, subprocess, collections
path, dwarf = sys.argv[1], sys.argv[2]; N = int(sys.argv[3]) if len(sys.argv) > 3 else 40
lines = open(path).read().split('\n'); slide = int(re.search(r'slide=0x([0-9a-f]+)', lines[0]).group(1), 16); load = 0x100000000 + slide
recs = [(int(p[0]), int(p[1]), [int(x, 16) for x in p[2:]]) for p in (l.split() for l in lines[1:]) if len(p) >= 3 and not p[2].startswith('size:')]
addrs = sorted({a for _, _, fr in recs for a in fr}); sym = {}
for i in range(0, len(addrs), 2000):
    ch = addrs[i:i+2000]; out = subprocess.run(['atos', '-arch', 'arm64', '-o', dwarf, '-l', hex(load)] + [hex(a) for a in ch], capture_output=True, text=True).stdout.split('\n')
    for a, s in zip(ch, out): sym[a] = re.sub(r' \(in [^)]*\)| \+ \d+$', '', s.strip())
def nimname(n):  # demangle app95serviceZserviceZchatZdtoZchat -> app/service/service/chat/dto/chat
    m = re.match(r'(\w+?)___?(.+?)_u\d+$', n)
    if not m: return n
    return m.group(1) + ' @ ' + m.group(2).replace('95', '_').replace('Z', '/')
GEN = ('nimAsgnStrV2', 'eqdup___system', 'nimNewObj', 'newSeqPayload', 'rawNewString', 'setLengthStr', 'resizeString', 'prepareSeqAdd', 'nimCopyMem', 'copyMem', 'eqcopy___system', 'QArrayData', 'QString::', 'QByteArray::', 'QHash', 'QMap', 'QList', 'QVarLength', 'std::__1', 'QtPrivate', 'QStringHashData', 'QLinkedStringHash', 'QSet', 'QVector')
def owner(fr):
    names = [sym.get(a, hex(a)) for a in fr]
    for n in names:
        if ('__app' in n or 'ZappZ' in n or '__src' in n) and not n.startswith(('eqdup___app95serviceZserviceZchatZdto',)) : return 'nim ' + nimname(n)
    for n in names:
        if 'eqdup' in n or 'eqcopy' in n: return 'nim ' + nimname(n)
    for n in names:
        if n.startswith(('Q', 'Status', 'sqlite', 'sqlcipher', 'hb_', '_hb')) and not any(g in n for g in GEN): return 'c++ ' + n.split('(')[0][:70]
    for n in names:
        if not n.startswith('0x') and not any(g in n for g in GEN): return 'other ' + n.split('(')[0][:70]
    return 'unknown'
agg = collections.Counter(); cnt = collections.Counter(); total = 0
for b, c, fr in recs: o = owner(fr); agg[o] += b; cnt[o] += c; total += b
print(f"total {total/1048576:.1f}MB")
for k, v in agg.most_common(N): print(f"{v/1048576:7.1f}MB {100*v/total:5.1f}% {cnt[k]:8d}  {k}")
