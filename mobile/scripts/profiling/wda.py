#!/usr/bin/env python3
"""wda.py <cmd> — drive the iPad through a WebDriverAgent server (WDA_URL env, default from wda-url.txt).
 tree | find <substr> | tap <substr> [n] | tapxy <x> <y> | text <str> | shot <name> | home | launch | kill"""
import sys, os, json, base64, subprocess, urllib.request
S = os.path.dirname(os.path.abspath(__file__))
URL = os.environ.get('WDA_URL') or open(os.path.join(S, 'wda-url.txt')).read().strip()
BID = 'app.status.mobile'
def req(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(URL + path, data=data, method=method, headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(r, timeout=120) as resp: return json.loads(resp.read().decode())
def session():
    p = os.path.join(S, 'wda-session.txt')
    if os.path.exists(p):
        sid = open(p).read().strip()
        try: req('GET', f'/session/{sid}/wda/screen'); return sid
        except Exception: pass
    sid = req('POST', '/session', {'capabilities': {'alwaysMatch': {}}})['sessionId']
    open(p, 'w').write(sid); return sid
def walk(n, out):
    if n.get('isVisible') in (True, '1', 1) or True:
        lbl = ' | '.join(x for x in (n.get('label'), n.get('name'), n.get('value')) if x)
        r = n.get('rect') or {}
        if lbl.strip(): out.append((n.get('type'), lbl, r, n.get('isEnabled')))
    for c in n.get('children', []) or []: walk(c, out)
def elems(sid):
    src = req('GET', f'/session/{sid}/source?format=json')['value']; out = []; walk(src, out); return out
def fmt(es): return '\n'.join(f"{i}: [{t}] {l[:90]} @({r.get('x',0):.0f},{r.get('y',0):.0f} {r.get('width',0):.0f}x{r.get('height',0):.0f}) enabled={en}" for i,(t,l,r,en) in enumerate(es))
def tapxy(sid, x, y):
    req('POST', f'/session/{sid}/actions', {'actions': [{'type': 'pointer', 'id': 'f', 'parameters': {'pointerType': 'touch'}, 'actions': [
        {'type': 'pointerMove', 'duration': 0, 'x': x, 'y': y}, {'type': 'pointerDown', 'button': 0}, {'type': 'pause', 'duration': 80}, {'type': 'pointerUp', 'button': 0}]}]})
c = sys.argv[1]
if c == 'launch': sid = session(); print(req('POST', f'/session/{sid}/wda/apps/launch', {'bundleId': BID})); sys.exit()
if c == 'kill': sid = session(); print(req('POST', f'/session/{sid}/wda/apps/terminate', {'bundleId': BID})); sys.exit()
sid = session()
if c == 'tree': print(fmt(elems(sid)))
elif c == 'find': print(fmt([e for e in elems(sid) if sys.argv[2].lower() in e[1].lower()]))
elif c == 'tap':
    es = [e for e in elems(sid) if sys.argv[2].lower() in e[1].lower()]; n = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    if not es: print('NOT FOUND', sys.argv[2]); sys.exit(1)
    t, l, r, en = es[n]; x, y = r['x'] + r['width'] / 2, r['y'] + r['height'] / 2; tapxy(sid, x, y); print(f'tap {l[:60]} @({x:.0f},{y:.0f})')
elif c == 'tapxy': tapxy(sid, float(sys.argv[2]), float(sys.argv[3])); print('ok')
elif c == 'text': print(req('POST', f'/session/{sid}/wda/keys', {'value': list(sys.argv[2])})['value'])
elif c == 'home': print(req('POST', f'/wda/homescreen', {}))
elif c == 'shot':
    p = os.path.join(S, sys.argv[2] + '.png'); open(p, 'wb').write(base64.b64decode(req('GET', f'/session/{sid}/screenshot')['value']))
    subprocess.run(['sips', '-Z', '900', p, '--out', p.replace('.png', '-s.png')], capture_output=True); print(p.replace('.png', '-s.png'))
