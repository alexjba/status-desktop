#!/bin/bash
# malloc-dump.sh <label> — ask the profiling build on the iPad for a live-malloc-block dump (pushes the trigger file
# into the app's log dir), pull it and symbolise it against the dSYM. Prints the ranked summary.
S=/private/tmp/claude-501/-Users-alexjbanca-Repos-status-desktop/bc83e5da-a729-4aaf-8e23-239567339916/scratchpad
W=/Users/alexjbanca/Repos/status-desktop.worktrees/perf-21528-memory
D=00008101-000474143446001E; LOGS="Library/Application Support/Status/logs"; L=${1:-snap}; OUT=$S/ios/devmem
: > $S/ios/dump-malloc
xcrun devicectl device copy to --device $D --domain-type appDataContainer --domain-identifier app.status.mobile --source $S/ios/dump-malloc --destination "$LOGS/dump-malloc" >/dev/null 2>&1 || { echo "push failed"; exit 1; }
for i in $(seq 1 24); do
  sleep 5
  if xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier app.status.mobile --source "$LOGS/malloc-blocks-latest.txt" --destination $OUT/$L-malloc-blocks.txt >/dev/null 2>&1; then
    if [ -z "$(xcrun devicectl device info files --device $D --domain-type appDataContainer --domain-identifier app.status.mobile 2>/dev/null | grep 'logs/dump-malloc ')" ]; then break; fi
  fi
done
head -1 $OUT/$L-malloc-blocks.txt
python3 $S/ios/mallocsym.py $OUT/$L-malloc-blocks.txt $W/mobile/build/ios/qt6/Release-iphoneos/Status.app.dSYM/Contents/Resources/DWARF/Status ${2:-30} | tee $OUT/$L-malloc-top.txt
