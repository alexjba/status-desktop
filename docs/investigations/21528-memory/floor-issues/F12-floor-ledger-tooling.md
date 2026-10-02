# Floor measurement tooling: page-table residency, APK attribution, cold-cache rows

**Expected gain:** none directly (tooling). Enables every other row: warm/cold PSS at login screen and settle, both processes, unreclaimable vs clean split, build ids. Required before any floor PR can carry its before/after table.

Context: status-im/status-app#21528. The pre-login floor was measured with ad-hoc tools (`mobile/scripts/profiling/floor/`):
`pmscan` reads page-present bits from `/proc/pid/pagemap` inside the app uid (needs the debuggable package),
`apkattr.py`/`pmattr.py` attribute `base.apk` pages to libraries, ELF sections and embedded Qt resources,
`smapsagg.py` summarises smaps, `qmlreach.py` computes the static QML reference closure. Two caveats must be part of
every row: fault-around inflates clean-page residency when the page cache is hot, and the debuggable package has
~15% larger code than release.

## What to build

A `floor-snap.sh` that, for a given package and label, relaunches to the login screen, records dumpsys totals for both
processes, the smaps aggregate, the APK attribution (when debuggable), and appends a ledger row with the
unreclaimable / clean-file split; a `--cold` mode that evicts the page cache first (reboot, or read a file larger than
RAM) and records a second row; the same two rows at the settle point. Documented in `floor.md`.

## Acceptance criteria

- [ ] One command produces the warm and cold rows for login screen and settle for both processes
- [ ] Rows carry the build id of `libStatus` and `libstatus.so`
- [ ] Used by every F-issue PR for its before/after table

## Blocked by

Nothing.
