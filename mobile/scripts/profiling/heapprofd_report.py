#!/usr/bin/env python3
"""Summarise a heapprofd .pftrace: bytes still allocated at the end of the trace (sum of the
per-dump alloc/free deltas), per process, grouped by leaf frame, by library, and by callstack.

Symbolisation: run `traceconv symbolize` first for libraries that carry a build-id (Qt, StatusQ,
libStatus) and concatenate the output onto the trace. Libraries linked without a build-id
(libnim_status_client.so) show up as a bare `base.apk` mapping; pass --apk and --libdir so the
mapping is matched to its .so by APK data offset and symbolised with llvm-symbolizer.

usage: heapprofd_report.py trace.pftrace [--apk app.apk --libdir DIR ...] [--top 40] [--depth 10]
       [--contains SUBSTR] [--symbolizer llvm-symbolizer]
"""
import argparse
import collections
import os
import struct
import subprocess
import sys
import zipfile

from perfetto.trace_processor import TraceProcessor


def apk_so_offsets(apk):
    z = zipfile.ZipFile(apk)
    out = {}
    for i in z.infolist():
        if not i.filename.endswith(".so"):
            continue
        z.fp.seek(i.header_offset)
        n, e = struct.unpack("<HH", z.fp.read(30)[26:30])
        out[i.header_offset + 30 + n + e] = os.path.basename(i.filename)
    return out


def find_lib(name, libdirs):
    for d in libdirs:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None


def llvm_symbolize(symbolizer, obj, addrs):
    if not addrs:
        return {}
    inp = "\n".join(f"0x{a:x}" for a in addrs) + "\n"
    try:
        res = subprocess.run([symbolizer, f"--obj={obj}", "--functions=short", "--demangle",
                              "--no-inlines", "--output-style=GNU"],
                             input=inp, capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"symbolizer failed for {obj}: {e}", file=sys.stderr)
        return {}
    names = [blk.split("\n")[0] for blk in res.strip().split("\n\n")]
    return {a: n for a, n in zip(addrs, names) if n and n != "??"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--apk")
    ap.add_argument("--libdir", action="append", default=[])
    ap.add_argument("--symbolizer", default="llvm-symbolizer")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--depth", type=int, default=10)
    ap.add_argument("--contains", default=None)
    args = ap.parse_args()

    tp = TraceProcessor(trace=args.trace)

    mappings = {r.id: r for r in tp.query(
        "select id, name, build_id, start_offset, exact_offset from stack_profile_mapping")}
    offsets = apk_so_offsets(args.apk) if args.apk else {}

    frames = {}
    by_mapping = collections.defaultdict(list)
    for r in tp.query("select id, mapping, rel_pc, coalesce(deobfuscated_name, name, '') as name from stack_profile_frame"):
        frames[r.id] = [r.name, r.mapping, r.rel_pc]
        if not r.name:
            by_mapping[r.mapping].append(r.id)

    # symbolise build-id-less APK mappings by offset
    for mid, fids in by_mapping.items():
        m = mappings.get(mid)
        if m is None or not (m.name or "").endswith("base.apk"):
            continue
        so = offsets.get(m.start_offset) or offsets.get(m.exact_offset)
        if not so:
            continue
        mappings[mid] = m  # keep
        setattr(m, "libname", so)
        path = find_lib(so, args.libdir)
        if not path:
            continue
        names = llvm_symbolize(args.symbolizer, path, sorted({frames[f][2] for f in fids}))
        for f in fids:
            frames[f][0] = names.get(frames[f][2], "")

    def modname(mid):
        m = mappings.get(mid)
        if m is None:
            return "?"
        lib = getattr(m, "libname", None)
        if lib:
            return lib
        n = m.name or "?"
        return n.rsplit("!", 1)[-1].rsplit("/", 1)[-1]

    def fname(fid):
        n, mid, pc = frames.get(fid, ("", None, 0))
        return f"{n or f'0x{pc:x}'} [{modname(mid)}]"

    parent, cs_frame = {}, {}
    for r in tp.query("select id, parent_id, frame_id from stack_profile_callsite"):
        parent[r.id], cs_frame[r.id] = r.parent_id, r.frame_id

    def stack(cs):
        out = []
        while cs is not None and len(out) < 64:
            out.append(fname(cs_frame.get(cs)))
            cs = parent.get(cs)
        return out

    procs = {r.upid: (r.pid, r.name or r.cmdline) for r in tp.query(
        "select upid, pid, name, cmdline from process where upid in (select distinct upid from heap_profile_allocation)")}

    for upid, (pid, name) in sorted(procs.items()):
        rows = list(tp.query(f"""
            select callsite_id, sum(size) as bytes, sum(count) as n
            from heap_profile_allocation where upid = {upid}
            group by callsite_id having sum(size) > 0"""))
        total = sum(r.bytes for r in rows)
        print(f"\n=== {name} (pid {pid}) still allocated at end of trace: {total/1e6:.1f} MB "
              f"(sampled, {len(rows)} callsites)")
        by_leaf, by_lib, by_stack = collections.Counter(), collections.Counter(), collections.Counter()
        for r in rows:
            st = stack(r.callsite_id)
            if args.contains and not any(args.contains in f for f in st):
                continue
            by_leaf[st[0]] += r.bytes
            # first non-allocator frame's library
            lib = next((f.split(" [")[-1].rstrip("]") for f in st
                        if not any(k in f for k in ("malloc", "calloc", "realloc", "operator new", "scudo", "[libc.so]", "[libc++"))), "?")
            by_lib[lib] += r.bytes
            by_stack[" <- ".join(st[: args.depth])] += r.bytes
        print("\n-- by owning library (first frame outside the allocator) --")
        for lib, b in by_lib.most_common(20):
            print(f"{b/1e6:8.1f} MB  {lib}")
        print("\n-- by leaf frame --")
        for leaf, b in by_leaf.most_common(args.top):
            print(f"{b/1e6:8.1f} MB  {leaf}")
        print("\n-- by callstack (leaf first) --")
        for st, b in by_stack.most_common(args.top):
            print(f"{b/1e6:8.1f} MB  {st}")


if __name__ == "__main__":
    sys.exit(main())
