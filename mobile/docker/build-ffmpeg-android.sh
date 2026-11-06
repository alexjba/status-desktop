#!/usr/bin/env bash
# Build shared FFmpeg for Android arm64-v8a, for QtMultimedia's FFmpeg backend (QCamera, QR scanning).
# Configure flags follow Qt's own Android FFmpeg build, minus OpenSSL (as in fdroid/build-ffmpeg.sh).
#
# Required env: FFMPEG_SRC_DIR (unpacked ffmpeg source) FFMPEG_DIR (install prefix) ANDROID_NDK_ROOT
set -euo pipefail

: "${FFMPEG_SRC_DIR:?}" "${FFMPEG_DIR:?}" "${ANDROID_NDK_ROOT:?}"
API=28
TOOLCHAIN=$(echo "$ANDROID_NDK_ROOT"/toolchains/llvm/prebuilt/*)
BUILD=$(mktemp -d)

cd "$BUILD"
"$FFMPEG_SRC_DIR/configure" \
    --prefix="$FFMPEG_DIR" \
    --enable-cross-compile --target-os=android --arch=aarch64 --cpu=armv8-a \
    --sysroot="$TOOLCHAIN/sysroot" \
    --cc="$TOOLCHAIN/bin/aarch64-linux-android$API-clang" \
    --cxx="$TOOLCHAIN/bin/aarch64-linux-android$API-clang++" \
    --ar="$TOOLCHAIN/bin/llvm-ar" --ranlib="$TOOLCHAIN/bin/llvm-ranlib" --strip="$TOOLCHAIN/bin/llvm-strip" \
    --nm="$TOOLCHAIN/bin/llvm-nm" \
    --enable-shared --disable-static --enable-pic \
    --disable-programs --disable-doc --disable-debug --disable-avdevice --disable-avfilter \
    --enable-network --disable-lzma --disable-vulkan --disable-v4l2-m2m \
    --disable-decoder=truemotion1 \
    --enable-jni --enable-mediacodec --enable-pthreads --enable-neon \
    --disable-asm --disable-indev=android_camera \
    --extra-ldflags="-Wl,-z,max-page-size=16384"
make -j"$(nproc)"
make install
cd /
rm -rf "$BUILD"
