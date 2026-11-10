#!/usr/bin/env python3
# Writes a copy of a .qrc without the QML files that import a given module, so they are
# neither compiled nor bundled. Entries keep their resource paths (as aliases of absolute
# file paths), so the copy can live anywhere under the same file name. Rewrites the copy
# only when it changes.
# usage: drop_qml_importing.py <in.qrc> <out.qrc> <module>
import re
import sys
from pathlib import Path

src, dst, module = Path(sys.argv[1]).resolve(), Path(sys.argv[2]), sys.argv[3]
imports = re.compile(r'^\s*import\s+%s(\s|$)' % re.escape(module), re.M)
entry = re.compile(r'<file(?:\s+alias="([^"]*)")?\s*>([^<]+)</file>')


def rewrite(m):
    alias, path = m.group(1) or m.group(2), m.group(2)
    full = (src.parent / path).resolve()
    if path.endswith('.qml') and imports.search(full.read_text()):
        return None
    return '<file alias="%s">%s</file>' % (alias, full)


out = []
for line in src.read_text().splitlines(keepends=True):
    m = entry.search(line)
    if m:
        new = rewrite(m)
        if new is None:
            continue
        line = line[:m.start()] + new + line[m.end():]
    out.append(line)
text = ''.join(out)
dst.parent.mkdir(parents=True, exist_ok=True)
if not dst.exists() or dst.read_text() != text:
    dst.write_text(text)
