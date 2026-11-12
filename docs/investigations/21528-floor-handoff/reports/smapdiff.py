import sys, re, collections
def load(f):
    t = collections.Counter(); name = ''
    for line in open(f, errors='replace'):
        m = re.match(r'^[0-9a-f]+-[0-9a-f]+ \S+ \S+ \S+ \S+\s*(.*)$', line)
        if m:
            name = m.group(1).strip() or '[anon]'
            name = re.sub(r'/data/app/[^ ]*/', '/data/app/…/', name)
            continue
        m = re.match(r'^(Pss|SwapPss):\s+(\d+) kB', line)
        if m: t[name] += int(m.group(2))
    return t
fs = sys.argv[1:]
d = [load(f) for f in fs]
keys = set().union(*d)
rows = sorted(keys, key=lambda k: -(d[-1][k] - d[0][k]))
print('MB: ' + ' | '.join(fs))
for k in rows[:30]:
    v = [x[k]/1024 for x in d]
    print(f'{v[-1]-v[0]:+7.1f}  ' + ' '.join(f'{x:7.1f}' for x in v) + '  ' + k[:90])
print('total', ' '.join(f'{sum(x.values())/1024:.1f}' for x in d))
