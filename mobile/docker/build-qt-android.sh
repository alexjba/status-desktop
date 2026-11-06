#!/usr/bin/env bash
# Build Qt for Android arm64-v8a from the per-module source tarballs, one module at a time.
# Shared libraries, packed relative relocations (RELR), -Bsymbolic-functions,
# identical code folding and thin LTO at -Oz: about 11 MB less Qt code in the APK and
# 6-7 MB less resident memory than the prebuilt Qt (status-im/status-app#21528).
#
# Required env: QT_VERSION QT_SRC_DIR (holds <module>-everywhere-src-$QT_VERSION.tar.xz)
#               QT_HOST_PATH ANDROID_SDK_ROOT ANDROID_NDK_ROOT FFMPEG_DIR OPENSSL_INCLUDE_DIR
# Optional env: QT_PREFIX QT_MODULES QT_BUILD_DIR QT_BUILD_JOBS
set -euo pipefail

: "${QT_VERSION:?}" "${QT_SRC_DIR:?}" "${QT_HOST_PATH:?}" "${ANDROID_SDK_ROOT:?}" "${ANDROID_NDK_ROOT:?}"
: "${FFMPEG_DIR:?}" "${OPENSSL_INCLUDE_DIR:?}"
QT_PREFIX="${QT_PREFIX:-/opt/qt/$QT_VERSION/android_arm64_v8a}"
QT_BUILD_DIR="${QT_BUILD_DIR:-/tmp/qt-build}"
QT_BUILD_JOBS="${QT_BUILD_JOBS:-$(nproc)}"
# Dependency order (dependencies.yaml): svg/imageformats/shadertools before declarative, websockets before webchannel.
QT_MODULES="${QT_MODULES:-qtshadertools qtsvg qtimageformats qtdeclarative qt5compat qtmultimedia qtwebsockets qtwebchannel qtpositioning qtwebview qtconnectivity qtlottie qtscxml}"

# The NDK's legacy toolchain file caches empty CMAKE_<LANG>_FLAGS before project(), so
# CMAKE_*_FLAGS_INIT never reach the compiler or linker. Pass the cache variables directly.
QT_LINK_FLAGS="-Wl,--pack-dyn-relocs=android+relr -Wl,--use-android-relr-tags -Wl,--icf=safe"
CMAKE_ARGS=(
    # -g0 overrides the toolchain's -g, which Qt only strips when it manages the optimization flags.
    -DCMAKE_C_FLAGS=-g0
    -DCMAKE_CXX_FLAGS=-g0
    -DCMAKE_SHARED_LINKER_FLAGS="$QT_LINK_FLAGS"
    -DCMAKE_MODULE_LINKER_FLAGS="$QT_LINK_FLAGS"
    # Qt switches Android to -O2 under LTCG (QTBUG-89472, fixed in current lld): keep -Oz.
    -DQT_USE_DEFAULT_CMAKE_OPTIMIZATION_FLAGS=ON
    -DCMAKE_C_FLAGS_RELEASE="-Oz -DNDEBUG"
    -DCMAKE_CXX_FLAGS_RELEASE="-Oz -DNDEBUG"
    -DOPENSSL_INCLUDE_DIR="$OPENSSL_INCLUDE_DIR"
    -DCMAKE_MESSAGE_LOG_LEVEL=WARNING
    -Wno-dev
)

unpack() { # <module> -> prints the source dir
    tar -xf "$QT_SRC_DIR/$1-everywhere-src-$QT_VERSION.tar.xz" -C "$QT_BUILD_DIR/src"
    echo "$QT_BUILD_DIR/src/$1-everywhere-src-$QT_VERSION"
}

build_install() { # <module> <src dir>
    grep -q -- '--pack-dyn-relocs=android+relr' build.ninja || { echo "$1: RELR flags missing" >&2; exit 1; }
    cmake --build . --parallel "$QT_BUILD_JOBS"
    cmake --install . > /dev/null
    cd "$QT_BUILD_DIR"
    rm -rf "$QT_BUILD_DIR/build" "$2"
}

mkdir -p "$QT_BUILD_DIR/src"

src=$(unpack qtbase)
mkdir -p "$QT_BUILD_DIR/build" && cd "$QT_BUILD_DIR/build"
"$src/configure" \
    -prefix "$QT_PREFIX" \
    -release -opensource -confirm-license \
    -nomake examples -nomake tests \
    -platform android-clang \
    -android-ndk "$ANDROID_NDK_ROOT" -android-sdk "$ANDROID_SDK_ROOT" \
    -android-ndk-platform android-28 -android-abis arm64-v8a \
    -qt-host-path "$QT_HOST_PATH" \
    -openssl-runtime \
    -reduce-relocations -ltcg \
    -no-feature-sql -no-dbus \
    -- "${CMAKE_ARGS[@]}"
build_install qtbase "$src"

for m in $QT_MODULES; do
    src=$(unpack "$m")
    extra=(-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON)
    if [ "$m" = qtmultimedia ]; then
        extra+=(-DFFMPEG_DIR="$FFMPEG_DIR" -DFEATURE_ffmpeg=ON -DQT_DEFAULT_MEDIA_BACKEND=ffmpeg -DQT_DEPLOY_FFMPEG=ON)
    fi
    mkdir -p "$QT_BUILD_DIR/build" && cd "$QT_BUILD_DIR/build"
    "$QT_PREFIX/bin/qt-configure-module" "$src" -- "${CMAKE_ARGS[@]}" "${extra[@]}"
    build_install "$m" "$src"
done

rm -rf "$QT_BUILD_DIR"
