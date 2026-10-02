#!/usr/bin/env python3
"""mallocsym.py <malloc-blocks.txt> <dSYM DWARF binary> [N] — symbolise and rank live malloc blocks by owner frame,
by component and by full stack."""
import sys, re, subprocess, collections
path, dwarf = sys.argv[1], sys.argv[2]; N = int(sys.argv[3]) if len(sys.argv) > 3 else 30
lines = open(path).read().split('\n'); hdr = lines[0]
slide = int(re.search(r'slide=0x([0-9a-f]+)', hdr).group(1), 16); load = 0x100000000 + slide
recs = []
for ln in lines[1:]:
    p = ln.split()
    if len(p) < 3: continue
    recs.append((int(p[0]), int(p[1]), [int(x, 16) for x in p[2:]]))
addrs = sorted({a for _, _, fr in recs for a in fr})
sym = {}
for i in range(0, len(addrs), 2000):
    chunk = addrs[i:i + 2000]
    out = subprocess.run(['atos', '-arch', 'arm64', '-o', dwarf, '-l', hex(load)] + [hex(a) for a in chunk], capture_output=True, text=True).stdout.split('\n')
    for a, s in zip(chunk, out): sym[a] = s.strip()
def name(a):
    s = sym.get(a, hex(a)); s = re.sub(r' \(in [^)]*\)', '', s); s = re.sub(r' \+ \d+$', '', s)
    return s if not s.startswith('0x') else 'sys:' + s
SKIP = ('malloc', 'calloc', 'realloc', 'operator new', 'sys:', '_malloc', 'nimAlloc', 'alloc0Impl', 'allocImpl', 'allocSharedImpl', 'rawAlloc', 'newObj', 'nimNewObj', 'newSeq', 'rawNewString', 'rawNewObj', 'newObjNoInit', 'QArrayData::allocate', 'QArrayData::reallocateUnaligned', 'QtPrivate::', 'QHashPrivate::', 'QMapData', 'std::__1::', 'QString::', 'QByteArray::', 'QList<', 'QVarLengthArray', 'QHash<', 'QMap<', 'QVector<', 'QSet<', 'copyMem', 'setLengthStrV2', 'prepareSeqAdd', 'prepareAdd', 'nimCopyMem', 'eqdup', 'eqcopy', 'mnewString', 'addChar', 'resizeString', 'setLengthStr', 'unsureAsgnRef', 'QObjectPrivate', 'QMetaObject', 'QVariant::', 'QArrayDataPointer')
def component(stack):
    for a in stack:
        n = name(a)
        if n.startswith('sys:'): continue
        if 'sqlite3' in n or 'sqlcipher' in n: return 'sqlcipher'
        if n.startswith(('QV4', 'QQml', 'QQuick', 'QJS', 'QSG')): return 'qml/quick'
        if n.startswith('Q') or n.startswith('Status'): return 'qt/statusq'
        if re.search(r'__[a-zA-Z0-9]+_u\d+$', n) or '__' in n and 'nim' in n.lower() or n.startswith(('nim', 'seaqt', 'dos_', 'Dto', 'Item')): return 'nim'
        return 'other:' + n.split('(')[0][:40]
    return 'sys only'
def owner(stack):
    for a in stack:
        n = name(a)
        if not any(k in n for k in SKIP): return n
    return name(stack[0]) if stack else '?'
by_owner = collections.Counter(); by_comp = collections.Counter(); by_stack = collections.Counter(); cnt_owner = collections.Counter()
total = sum(b for b, _, _ in recs)
for b, c, fr in recs:
    o = owner(fr); by_owner[o] += b; cnt_owner[o] += c; by_comp[component(fr)] += b
    by_stack[' <- '.join(name(a)[:60] for a in fr[:6])] += b
print(hdr); print(f"total live {total/1048576:.1f}MB in {sum(c for _,c,_ in recs)} blocks with stacks\n== by component ==")
for k, v in by_comp.most_common(12): print(f"{v/1048576:8.1f}MB {100*v/total:5.1f}%  {k}")
print("\n== by owner frame ==")
for k, v in by_owner.most_common(N): print(f"{v/1048576:8.1f}MB {100*v/total:5.1f}% {cnt_owner[k]:8d}  {k[:120]}")
print("\n== by stack (top 6 frames) ==")
for k, v in by_stack.most_common(N // 2): print(f"{v/1048576:8.1f}MB  {k[:300]}")
