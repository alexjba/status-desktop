#!/bin/bash
# f03-collect.sh — copy per-variant vm summaries + QSG log excerpts into the repo and build the ledger CSV.
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad/ios
W=/Users/alexjbanca/Repos/status-desktop.worktrees/perf-21528-memory/docs/investigations/21528-memory
P=$W/profiles/floor; C=$W/ledger/f03-atlas-ipad.csv
echo "variant,env,point,phys_mb,resident_mb,internal_mb,external_mb,compressed_mb,iosurface_mb,gpu_virt_mb,gpu_res_mb,gpu_regions,malloc_dirty_mb,go_dirty_mb,jsc_mb,footprint_series_15s" > $C
for L in baseline-a baseline-b atlas1024 atlas1024-b atlas512 nodf transient combo; do
  [ -f $S/f03-$L.log ] || continue
  for P2 in login settle; do [ -f $S/f03/$L-$P2-vm.txt ] && cp $S/f03/$L-$P2-vm.txt $P/f03-$L-$P2-vm.txt; done
  { echo "# $L: $(grep '^launched' $S/f03-$L.run 2>/dev/null | sed 's/ log:.*//')"; grep -i "atlas\|scenegraph\|qt.rhi\|swapchain\|glyph\|QRhi\|Metal\|render loop" $S/f03-$L.log | sed 's/\x1b\[[0-9;]*m//g' | sed 's/.*text=//' | grep -v "animation driver" | cut -c1-400; echo; echo "# memfootprint series (15 s):"; grep memfootprint $S/f03-$L.log | sed 's/.*memfootprint //'; } > $P/f03-$L-qsg.txt
  env=$(grep '^launched' $S/f03-$L.run 2>/dev/null | sed 's/.*env: *//; s/ *log:.*//'); [ -z "$env" ] && env="(none)"
  series=$(grep memfootprint $S/f03-$L.log | sed 's/.*phys_mb=\([0-9]*\).*/\1/' | tr '\n' ' ')
  for P2 in login settle; do
    row=$(grep "^$L,$P2," $S/f03-$L.run 2>/dev/null || cat /private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/tasks/*.output 2>/dev/null | grep "^$L,$P2," | head -1)
    [ -z "$row" ] && continue
    python3 - "$L" "$env" "$P2" "$row" "$series" <<'PY'
import sys,re
L,env,P2,row,series=sys.argv[1:]
d=dict(kv.split('=',1) for kv in row.split(',')[2:] if '=' in kv)
fp=re.search(r'phys_mb=(\d+) resident_mb=(\d+)',row)
print(','.join([L,'"'+env+'"',P2,fp.group(1),fp.group(2),d['internal'],d['external'],d['compressed'],d['iosurface_res'],d['gpu_virt'],d['gpu_res'],d['gpu_regions'],d['malloc_dirty'],d['go_dirty'],d['jsc'],'"'+series.strip()+'"']))
PY
  done >> $C
done
cat $C
