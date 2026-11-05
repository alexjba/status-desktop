#!/usr/bin/env python3
"""Compare QML inventory dumps written by STATUS_QML_INVENTORY.

  qml_inventory_diff.py before.json after.json   deltas sorted by absolute size
  qml_inventory_diff.py dump.json                 top entries of a single dump
"""

import argparse
import json
import sys
from collections import Counter


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def short(url):
    """Drops the scheme and everything up to the last 'qml/' or 'ui/' segment."""
    if not url:
        return "-"
    for marker in ("/ui/", "/qml/"):
        index = url.rfind(marker)
        if index != -1:
            return url[index + 1:]
    return url.split("://", 1)[-1]


def tables(dump):
    by_type = Counter()
    by_owner = Counter()
    by_group = Counter()
    for group in dump.get("objects", []):
        count = group["count"]
        by_type[group["type"]] += count
        by_owner[short(group["owningFile"])] += count
        by_group[(group["type"], short(group["creationFile"]), short(group["owningFile"]))] += count

    unit_bytes = Counter()
    unit_objects = Counter()
    for unit in dump.get("compilationUnits", []):
        unit_bytes[short(unit["url"])] += unit["totalBytes"]
        unit_objects[short(unit["url"])] += unit["liveObjects"]

    return {
        "type": by_type,
        "owner": by_owner,
        "group": by_group,
        "unitBytes": unit_bytes,
        "unitObjects": unit_objects,
    }


def label(key):
    if isinstance(key, tuple):
        type_name, declared, owner = key
        return f"{type_name}  declared:{declared}  owner:{owner}"
    return key


def print_top(title, counter, limit):
    print(f"\n== {title} (total {sum(counter.values())})")
    for key, value in counter.most_common(limit):
        print(f"{value:>12}  {label(key)}")


def print_delta(title, before, after, limit, extra=None):
    keys = set(before) | set(after)
    rows = [(after.get(k, 0) - before.get(k, 0), k) for k in keys]
    rows = [row for row in rows if row[0]]
    rows.sort(key=lambda row: (-abs(row[0]), str(row[1])))
    total = sum(after.values()) - sum(before.values())
    print(f"\n== {title} (total {total:+})")
    for delta, key in rows[:limit]:
        suffix = f"  [{extra(key)}]" if extra else ""
        print(f"{delta:>+12}  {before.get(key, 0):>10} -> {after.get(key, 0):<10} {label(key)}{suffix}")


def top(dump, limit):
    t = tables(dump)
    print(f"tracked objects: {dump.get('trackedObjects')}  uptime: {dump.get('uptimeMs')} ms")
    print_top("objects per type", t["type"], limit)
    print_top("objects per owning file", t["owner"], limit)
    print_top("objects per (type, declared, owner)", t["group"], limit)
    print_top("compilation unit bytes", t["unitBytes"], limit)
    idle = Counter({url: size for url, size in t["unitBytes"].items() if not t["unitObjects"][url]})
    print_top("compilation unit bytes with 0 live objects", idle, limit)


def diff(before, after, limit):
    a, b = tables(before), tables(after)
    print(f"tracked objects: {before.get('trackedObjects')} -> {after.get('trackedObjects')}")
    print_delta("objects per type", a["type"], b["type"], limit)
    print_delta("objects per owning file", a["owner"], b["owner"], limit)
    print_delta("objects per (type, declared, owner)", a["group"], b["group"], limit)
    print_delta("compilation unit bytes", a["unitBytes"], b["unitBytes"], limit,
                extra=lambda url: f"live objects {a['unitObjects'].get(url, 0)} -> {b['unitObjects'].get(url, 0)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dumps", nargs="+", metavar="dump.json", help="one dump (top mode) or two (diff mode)")
    parser.add_argument("-n", "--limit", type=int, default=30, help="rows per table (default 30)")
    args = parser.parse_args()

    if len(args.dumps) == 1:
        top(load(args.dumps[0]), args.limit)
    elif len(args.dumps) == 2:
        diff(load(args.dumps[0]), load(args.dumps[1]), args.limit)
    else:
        parser.error("expected one or two dumps")


if __name__ == "__main__":
    sys.exit(main())
