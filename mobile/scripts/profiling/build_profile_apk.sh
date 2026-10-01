#!/usr/bin/env bash
# Build the profile APK without installing: root mobile-build target with the profile variables.
cd ~/Repos/status-desktop.worktrees/perf-21528-memory || exit 1
export QMAKE=/Users/alexjbanca/Qt/6.11.0/android_arm64_v8a/bin/qmake
export ANDROID_SERIAL=9b010059305330363400f1f217fa8c
export USE_SYSTEM_NIM=1
export PATH=/Users/alexjbanca/Repos/status-desktop/vendor/nimbus-build-system/vendor/Nim/bin:$PATH
make mobile-build PROFILE=1 GRADLE_TARGETS=assembleProfile QML_DEBUG_PORT=49152 -j10 V=3 USE_SYSTEM_NIM=1
echo "exit=$?"
