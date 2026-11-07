#!/usr/bin/env bash
# Build shared FFmpeg for Android arm64-v8a, for QtMultimedia's FFmpeg backend (QCamera, QR scanning,
# https media). Configure flags follow Qt's own Android FFmpeg build (coin install-ffmpeg-android.sh).
#
# OpenSSL is reached through Qt's FFmpeg stubs, as in Qt's prebuilt kit: libavformat must NEED
# libQt6FFmpegStub-{ssl,crypto}_arm64-v8a.so and its .pc must list -lQt6FFmpegStub-{ssl,crypto}, so that
# QtMultimedia builds the stubs, which load the app's libssl_3/libcrypto_3 at runtime. Qt's CI patches
# NEEDED with patchelf afterwards; here FFmpeg links against stand-in libraries carrying the stub
# SONAMEs, built from the static OpenSSL, so no shipped binary is rewritten.
#
# Required env: FFMPEG_SRC_DIR (unpacked ffmpeg source) FFMPEG_DIR (install prefix) ANDROID_NDK_ROOT
#               OPENSSL_DIR (OpenSSL build tree for android-arm64 with libssl.a, libcrypto.a, include/)
set -euo pipefail

: "${FFMPEG_SRC_DIR:?}" "${FFMPEG_DIR:?}" "${ANDROID_NDK_ROOT:?}" "${OPENSSL_DIR:?}"
API=28
ABI_SUFFIX=_arm64-v8a
TOOLCHAIN=$(echo "$ANDROID_NDK_ROOT"/toolchains/llvm/prebuilt/*)
CC="$TOOLCHAIN/bin/aarch64-linux-android$API-clang"
BUILD=$(mktemp -d)

mkdir "$BUILD/ssl"
"$CC" -shared -o "$BUILD/ssl/libcrypto.so" -Wl,-soname,libQt6FFmpegStub-crypto$ABI_SUFFIX.so \
    -Wl,--whole-archive "$OPENSSL_DIR/libcrypto.a" -Wl,--no-whole-archive
"$CC" -shared -o "$BUILD/ssl/libssl.so" -Wl,-soname,libQt6FFmpegStub-ssl$ABI_SUFFIX.so \
    -Wl,--whole-archive "$OPENSSL_DIR/libssl.a" -Wl,--no-whole-archive -L"$BUILD/ssl" -lcrypto

cd "$BUILD"
# Keep pkg-config away from host OpenSSL; configure then checks -lssl -lcrypto against the stand-ins.
PKG_CONFIG_LIBDIR=/nonexistent "$FFMPEG_SRC_DIR/configure" \
    --prefix="$FFMPEG_DIR" \
    --enable-cross-compile --target-os=android --arch=aarch64 --cpu=armv8-a \
    --sysroot="$TOOLCHAIN/sysroot" \
    --cc="$CC" \
    --cxx="$TOOLCHAIN/bin/aarch64-linux-android$API-clang++" \
    --ar="$TOOLCHAIN/bin/llvm-ar" --ranlib="$TOOLCHAIN/bin/llvm-ranlib" --strip="$TOOLCHAIN/bin/llvm-strip" \
    --nm="$TOOLCHAIN/bin/llvm-nm" \
    --enable-shared --disable-static --enable-pic \
    --disable-programs --disable-doc --disable-debug --disable-avdevice --disable-avfilter \
    --enable-network --disable-lzma --disable-vulkan --disable-v4l2-m2m \
    --disable-decoder=truemotion1 \
    --enable-jni --enable-mediacodec --enable-openssl --enable-pthreads --enable-neon \
    --disable-asm --disable-indev=android_camera \
    --extra-cflags="-I$OPENSSL_DIR/include" \
    --extra-ldflags="-L$BUILD/ssl -Wl,-z,max-page-size=16384"
make -j"$(nproc)"
make install

# Same .pc rewrite as Qt's fix_ffmpeg_dependencies.sh: tells QtMultimedia which stubs to build.
sed -i.bak -E '/^Libs.private:/s/ -l(ssl|crypto)/ -lQt6FFmpegStub-\1/g' "$FFMPEG_DIR"/lib/pkgconfig/*.pc
rm -f "$FFMPEG_DIR"/lib/pkgconfig/*.pc.bak
cd /
rm -rf "$BUILD"
