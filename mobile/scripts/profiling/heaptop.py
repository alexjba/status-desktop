#!/usr/bin/env python3
"""heaptop.py <legacy heap text> [inuse|alloc] [N] — rank a debug=1 Go heap dump by leaf function and by owner
(first frame outside runtime/stdlib), unsampled like pprof does."""
import sys, math, re, collections
path = sys.argv[1]; mode = sys.argv[2] if len(sys.argv) > 2 else 'inuse'; N = int(sys.argv[3]) if len(sys.argv) > 3 else 25
lines = open(path, errors='replace').read().split('\n')
rate = 524288
m = re.search(r'@ heap/(\d+)', lines[0]); rate = int(m.group(1)) // 2 if m else rate
leaf = collections.Counter(); owner = collections.Counter(); total = 0.0
rec = None; frames = []
def flush():
    global total
    if rec is None: return
    n, b, an, ab = rec
    cnt, byt = (an, ab) if mode == 'alloc' else (n, b)
    if cnt == 0 or byt == 0: return
    s = byt / cnt; scale = 1.0 / (1 - math.exp(-s / rate)) if s < rate * 50 else 1.0
    v = byt * scale; total += v
    fn = [f for f in frames]
    if fn: leaf[fn[0]] += v
    own = next((f for f in fn if not f.startswith(('runtime.', 'encoding/', 'bytes.', 'strings.', 'io.', 'fmt.', 'reflect.', 'math/', 'sync.', 'bufio.', 'net/', 'crypto/', 'compress/', 'golang.org/x/crypto', 'google.golang.org/protobuf', 'github.com/golang/protobuf', 'strconv.', 'unicode', 'sort.', 'os.', 'regexp', 'internal/', 'context.', 'time.', 'log.', 'hash/'))), fn[0] if fn else '?')
    owner[own] += v
for ln in lines[1:]:
    m = re.match(r'(\d+): (\d+) \[(\d+): (\d+)\] @', ln)
    if m: flush(); rec = tuple(int(x) for x in m.groups()); frames = []; continue
    m = re.match(r'#\t0x[0-9a-f]+\t(\S+?)\+0x[0-9a-f]+', ln)
    if m and rec is not None: frames.append(m.group(1))
flush()
print(f"{mode}: total {total/1048576:.1f}MB (unsampled)")
for title, c in (('by leaf function', leaf), ('by owner (first non-stdlib frame)', owner)):
    print(f"\n== {title} ==")
    for k, v in c.most_common(N): print(f"{v/1048576:8.2f}MB {100*v/total:5.1f}%  {k[:110]}")
