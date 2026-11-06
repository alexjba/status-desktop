# Multi-stage Dockerfile for Status Desktop Android Build

# =============================================================================
# Global build arguments
# =============================================================================
ARG QT_VERSION="6.11.0"
ARG QT_MODULES="qt5compat qtmultimedia qtwebsockets qtpositioning qtshadertools qtimageformats qtwebview qtconnectivity qtlottie qtscxml qtwebchannel"
ARG JAVA_VERSION=17
ARG ANDROID_API_LEVEL=36
ARG ANDROID_NDK_VERSION=27.2.12479018
ARG GOLANG_VERSION="1.24.7"
ARG NIM_VERSION="2.2.10"
ARG NIMBLE_VERSION="0.26.1"
ARG NIX_VERSION="2.24.11"
# Android Qt modules built from source, in dependency order.
ARG QT_ANDROID_MODULES="qtshadertools qtsvg qtimageformats qtdeclarative qt5compat qtmultimedia qtwebsockets qtwebchannel qtpositioning qtwebview qtconnectivity qtlottie qtscxml"
ARG FFMPEG_VERSION="7.1.3"
ARG OPENSSL_VERSION="3.0.15"

# =============================================================================
# Stage 1: host Qt (gcc_64) using aqtinstall
# =============================================================================
FROM ubuntu:noble AS qt-host

ARG QT_VERSION QT_MODULES

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 python3-pip python3-venv build-essential \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN chmod -R 777 /opt

# The host Qt provides the build tools (moc, qmlcachegen, qsb, qscxmlc, androiddeployqt...)
# for the Android Qt built from source in the next stage.
RUN python3 -m venv /tmp/aqt-venv \
 && /tmp/aqt-venv/bin/pip install -U pip setuptools aqtinstall \
 && /tmp/aqt-venv/bin/aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 -m ${QT_MODULES} -O /opt/qt --timeout 3000 \
 && rm -rf /tmp/aqt-venv

RUN rm -rf /opt/qt/${QT_VERSION}/gcc_64/qml/QtWebEngine \
 && rm -rf /opt/qt/${QT_VERSION}/gcc_64/lib/libQt6WebEngine* \
 && rm -rf /opt/qt/${QT_VERSION}/gcc_64/lib/cmake/Qt6WebEngine* \
 && rm -rf /opt/qt/${QT_VERSION}/gcc_64/libexec/QtWebEngineProcess \
 && rm -rf /opt/qt/${QT_VERSION}/gcc_64/modules/WebEngine*.json \
 # Remove QtWebEngine dependency from QtWebView qmldir (aqtinstall's QtWebView was built with WebEngine support)
 && sed -i '/depends QtWebEngine/d' /opt/qt/${QT_VERSION}/gcc_64/qml/QtWebView/qmldir 2>/dev/null || true

# Clean up unnecessary Qt files to reduce image size (docs, examples)
# Note: Do NOT delete .a files - Qt CMake configs reference them and builds will fail
RUN rm -rf /opt/qt/${QT_VERSION}/gcc_64/doc \
 && rm -rf /opt/qt/${QT_VERSION}/gcc_64/examples \
 # Remove debug symbols from Qt libraries
 && find /opt/qt/${QT_VERSION} -name "*.debug" -type f -delete 2>/dev/null || true

# =============================================================================
# Stage 2: Android Qt (arm64-v8a) built from source
# =============================================================================
FROM ubuntu:noble AS qt-android

ARG QT_VERSION QT_ANDROID_MODULES JAVA_VERSION ANDROID_API_LEVEL ANDROID_NDK_VERSION
ARG FFMPEG_VERSION OPENSSL_VERSION
# Parallel jobs for the Qt build; LTO links need about 2 GB each.
ARG QT_BUILD_JOBS

ENV DEBIAN_FRONTEND=noninteractive \
    ANDROID_SDK_ROOT=/opt/android-sdk \
    ANDROID_NDK_ROOT=/opt/android-sdk/ndk/${ANDROID_NDK_VERSION} \
    JAVA_HOME=/usr/lib/jvm/java-${JAVA_VERSION}-openjdk-amd64

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build perl python3 pkg-config curl wget ca-certificates xz-utils unzip \
    openjdk-${JAVA_VERSION}-jdk google-android-cmdline-tools-13.0-installer \
    # Runtime libraries of the host Qt tools (qsb links QtGui)
    libgl1 libegl1 libglib2.0-0t64 libxkbcommon0 libfontconfig1 libfreetype6 libdbus-1-3 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN yes | sdkmanager --sdk_root=/opt/android-sdk \
    "platforms;android-${ANDROID_API_LEVEL}" \
    "build-tools;${ANDROID_API_LEVEL}.0.0" \
    "ndk;${ANDROID_NDK_VERSION}" > /dev/null

COPY --from=qt-host /opt/qt/${QT_VERSION}/gcc_64 /opt/qt/${QT_VERSION}/gcc_64

# Fail early if a host tool the Android build runs cannot load.
RUN for t in bin/qsb bin/qmake libexec/moc libexec/rcc libexec/qmlcachegen libexec/qmltyperegistrar libexec/qscxmlc; do \
      ! ldd /opt/qt/${QT_VERSION}/gcc_64/$t | grep 'not found' || exit 1; \
    done

# Sources, pinned by SHA-256.
# FFmpeg 7.1.3 matches the FFmpeg that Qt ${QT_VERSION}'s prebuilt Android binaries use.
WORKDIR /src
RUN set -e; \
    QT_URL="https://download.qt.io/official_releases/qt/${QT_VERSION%.*}/${QT_VERSION}/submodules"; \
    mkdir qt; \
    for entry in \
      qtbase=231ad85979864d914dc9568a1b71c91d6cf20d7b2021d059103bf0eb51cb755e \
      qtshadertools=e43cb1ae8809b2a858281ee269f98da59d0fc1bcf958ca5510c81f7ad3d2e14a \
      qtdeclarative=4eece569431ddf8324e7d322fa27001916570b23df535f8fb28aba445eedfde9 \
      qtsvg=dfa8d653be07087d9407ed4a4ebae847f8953e0b7abd829f089803ab652a30e6 \
      qtimageformats=d3adb02ac5e2fe24068dbdaee0d7cc68cc3fa8553291c1bfce77c9fe8e940cc8 \
      qt5compat=e62954646b2749723aa5c7db32faab407358734075590058a01e793382d4c63e \
      qtmultimedia=90c4cac0a7a983b68d1b0873d0714e7873b9a493403fa8593e8a4eea3ea26040 \
      qtwebsockets=569f10d1fb35195869576004f5b5ff09735d2f0319e2e8f0dd0f40c7ec31d032 \
      qtwebchannel=e946143a8b015e2c9d5cc6110515f43618b441799da546138d0b05d8afa9fb24 \
      qtpositioning=d61fd0985ede513ec34d2d1c1e92f383eb8eb46678ca9da805cf795cccb796e9 \
      qtwebview=cb0eaed94a12d5f650863d346c423e9f4383dbce1d05866869c40118c6e8c4b3 \
      qtconnectivity=c0f0c124c849ef811a873bf1a0123e3feabac6e9ca3ea7e7ac7a40543ec6193a \
      qtlottie=2f23ae6879bc652e194f3a7ba5972d13ec04f8f48226e8c00a002f3227bdcd0a \
      qtscxml=6c383a53c0c3668fcc80d89f00193f0e928a784199c591213cbed1bf2f64d4e7; do \
      f="${entry%%=*}-everywhere-src-${QT_VERSION}.tar.xz"; \
      curl -sSfL -o "qt/$f" "$QT_URL/$f"; \
      echo "${entry#*=}  qt/$f" | sha256sum -c -; \
    done; \
    curl -sSfL -o ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"; \
    echo "f0bf043299db9e3caacb435a712fc541fbb07df613c4b893e8b77e67baf3adbe  ffmpeg.tar.xz" | sha256sum -c -; \
    tar -xf ffmpeg.tar.xz && rm ffmpeg.tar.xz; \
    curl -sSfL -o openssl.tar.gz "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"; \
    echo "23c666d0edf20f14249b3d8f0368acaee9ab585b09e1de82107c66e1f3ec9533  openssl.tar.gz" | sha256sum -c -; \
    tar -xzf openssl.tar.gz && rm openssl.tar.gz

# Qt loads OpenSSL at runtime (the app ships libcrypto_3/libssl_3); it only needs the generated headers.
RUN cd openssl-${OPENSSL_VERSION} \
 && PATH=${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH \
    ./Configure android-arm64 -D__ANDROID_API__=28 no-shared no-tests > /dev/null \
 && make build_generated > /dev/null

COPY build-ffmpeg-android.sh build-qt-android.sh /src/

RUN FFMPEG_SRC_DIR=/src/ffmpeg-${FFMPEG_VERSION} FFMPEG_DIR=/opt/ffmpeg ./build-ffmpeg-android.sh

RUN QT_SRC_DIR=/src/qt QT_HOST_PATH=/opt/qt/${QT_VERSION}/gcc_64 FFMPEG_DIR=/opt/ffmpeg \
    OPENSSL_INCLUDE_DIR=/src/openssl-${OPENSSL_VERSION}/include QT_MODULES="${QT_ANDROID_MODULES}" \
    QT_BUILD_JOBS="${QT_BUILD_JOBS:-$(nproc)}" \
    ./build-qt-android.sh

# =============================================================================
# Stage 3: Final Build Image
# =============================================================================
FROM ubuntu:noble

ARG QT_VERSION
ARG JAVA_VERSION
ARG ANDROID_API_LEVEL
ARG ANDROID_NDK_VERSION
ARG GOLANG_VERSION
ARG NIM_VERSION
ARG NIMBLE_VERSION
ARG NIX_VERSION

ENV DEBIAN_FRONTEND=noninteractive

# Qt environment variables
ENV QT_PATH=/opt/qt \
    QT_VERSION=${QT_VERSION}

ENV QTDIR=${QT_PATH}/${QT_VERSION}/android_arm64_v8a
ENV QT_HOST_PATH=${QT_PATH}/${QT_VERSION}/gcc_64
ENV PATH="${QT_PATH}/${QT_VERSION}/android_arm64_v8a/bin:${QT_PATH}/${QT_VERSION}/gcc_64/bin:${QT_PATH}/${QT_VERSION}/gcc_64/libexec:${PATH}"
ENV QT_PLUGIN_PATH="${QT_PATH}/${QT_VERSION}/gcc_64/plugins"

# Android environment variables
ENV JAVA_HOME=/usr/lib/jvm/java-${JAVA_VERSION}-openjdk-amd64
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV ANDROID_NDK_ROOT=/opt/android-sdk/ndk/${ANDROID_NDK_VERSION}

# Locale
ENV LANG=en_US.UTF-8 \
    LANGUAGE=en_US:en \
    LC_ALL=en_US.UTF-8

# =============================================================================
# Install system dependencies
# =============================================================================
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build essentials
    build-essential cmake ninja-build pkg-config extra-cmake-modules \
    autoconf automake libtool gcc-13 g++-13 \
    # Dev tools
    git git-lfs curl wget unzip file xz-utils jq rsync s3cmd fuse \
    gnupg2 openssh-client ca-certificates locales sudo \
    software-properties-common \
    # Python
    python3 python3-pip python3-venv \
    # Libraries for Qt
    libgl1-mesa-dev libsm6 libice6 libfontconfig1 libdbus-1-3 \
    libssl-dev libz-dev zlib1g-dev \
    libxext6 libxrender1 libxkbcommon-dev libxkbcommon-x11-dev \
    libxcomposite1 libxtst6 libxrandr2 libxcursor1 libxi6 \
    libxcb-randr0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-render-util0 libxcb-shape0 libxcb-render0 libxcb-xinerama0 \
    libxcb-cursor0 libxslt1.1 libxkbfile1 \
    libatk1.0-0 libcups2 libgtk-3-0 libgdk-pixbuf-2.0-0 \
    # GStreamer
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-alsa \
    libpulse-mainloop-glib0 gstreamer1.0-pulseaudio libgstreamer-plugins-base1.0-0 \
    # Database
    libsqlite3-dev libreadline-dev libncurses5-dev libncursesw5-dev \
    unixodbc-dev libpq-dev libbz2-dev \
    # Other
    libnss3 llvm tk-dev \
    # Java and Android
    openjdk-${JAVA_VERSION}-jdk google-android-cmdline-tools-13.0-installer \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Configure locale
# =============================================================================
RUN sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen && locale-gen

# =============================================================================
# Install Android SDK and NDK
# =============================================================================
RUN yes | sdkmanager --sdk_root=/opt/android-sdk \
    "platforms;android-${ANDROID_API_LEVEL}" \
    "platform-tools" \
    "build-tools;${ANDROID_API_LEVEL}.0.0" \
 && yes | sdkmanager --sdk_root=/opt/android-sdk "ndk;${ANDROID_NDK_VERSION}" \
 && yes | sdkmanager --licenses \
 # Clean up NDK unused files (samples, shader-tools, simpleperf) to save ~500MB
 && rm -rf /opt/android-sdk/ndk/${ANDROID_NDK_VERSION}/shader-tools \
 && rm -rf /opt/android-sdk/ndk/${ANDROID_NDK_VERSION}/simpleperf \
 && rm -rf /opt/android-sdk/ndk/${ANDROID_NDK_VERSION}/sources/cxx-stl/llvm-libc++/test \
 && rm -rf /opt/android-sdk/.android

# =============================================================================
# Copy Qt from install stage
# =============================================================================
RUN chmod -R 777 /opt && mkdir -p /opt/qt/${QT_VERSION}
COPY --from=qt-host /opt/qt/${QT_VERSION}/gcc_64 /opt/qt/${QT_VERSION}/gcc_64/
COPY --from=qt-android /opt/qt/${QT_VERSION}/android_arm64_v8a /opt/qt/${QT_VERSION}/android_arm64_v8a/

# Configure Qt library path
RUN echo "/opt/qt/${QT_VERSION}/gcc_64/lib" > /etc/ld.so.conf.d/qt.conf && ldconfig

# =============================================================================
# Install Golang
# =============================================================================
RUN GOLANG_SHA256="da18191ddb7db8a9339816f3e2b54bdded8047cdc2a5d67059478f8d1595c43f" \
 && GOLANG_TARBALL="go${GOLANG_VERSION}.linux-amd64.tar.gz" \
 && wget -q "https://dl.google.com/go/${GOLANG_TARBALL}" \
 && echo "${GOLANG_SHA256} ${GOLANG_TARBALL}" | sha256sum -c \
 && tar -C /usr/local -xzf "${GOLANG_TARBALL}" \
 && rm "${GOLANG_TARBALL}" \
 && ln -sf /usr/local/go/bin/go /usr/local/bin/go \
 && ln -sf /usr/local/go/bin/gofmt /usr/local/bin/gofmt

# Install Go tools and clean up Go module cache to save space
RUN /usr/local/go/bin/go install github.com/go-bindata/go-bindata/v3/go-bindata@latest \
 && /usr/local/go/bin/go install go.uber.org/mock/mockgen@v0.4.0 \
 && cp /root/go/bin/* /usr/local/bin/ \
 && rm -rf /root/go /root/.cache/go-build

# =============================================================================
# Install Protoc (from GitHub releases, includes well-known types)
# =============================================================================
RUN PROTOC_SHA256="bc8211ce760bd43ee21ddc145d6d9dbaeeabae205267a79d9054a240e367d4b4" \
 && PROTOC_TARBALL="protoc-36.0-linux-x86_64.zip" \
 && wget -q -L "https://github.com/protocolbuffers/protobuf/releases/download/v36.0/${PROTOC_TARBALL}" \
 && echo "${PROTOC_SHA256} ${PROTOC_TARBALL}" | sha256sum -c \
 && unzip -d /usr/local "${PROTOC_TARBALL}" \
 && rm "${PROTOC_TARBALL}"

# =============================================================================
# Create Jenkins user
# =============================================================================
RUN groupadd -g 1001 jenkins \
 && useradd --create-home -u 1001 -g 1001 -s /bin/bash jenkins \
 && echo "jenkins ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers \
 && mkdir /nix && chown jenkins:jenkins /nix

# =============================================================================
# Switch to jenkins user for user-specific installations
# =============================================================================
USER jenkins
WORKDIR /home/jenkins
ENV HOME="/home/jenkins"

# Build OpenSSL for Android arm64-v8a (only keep lib and include)
RUN set -e \
 && git clone --depth 1 --branch openssl-3.0.15 https://github.com/openssl/openssl.git \
 && mkdir -p openssl-output/arm64-v8a \
 && cd openssl \
 && export ANDROID_NDK_HOME=${ANDROID_NDK_ROOT} \
 && export PATH=${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH \
 && ./Configure android-arm64 \
    -D__ANDROID_API__=21 \
    --prefix=/home/jenkins/openssl-output/arm64-v8a \
    --openssldir=/home/jenkins/openssl-output/arm64-v8a \
    no-shared no-tests \
 && make -j$(nproc) \
 && make install_sw \
 && cd .. && rm -rf openssl \
 # Only keep lib and include, remove ssl config files
 && rm -rf /home/jenkins/openssl-output/arm64-v8a/bin \
 && rm -rf /home/jenkins/openssl-output/arm64-v8a/ssl

ENV OPENSSL_LIB_DIR="/home/jenkins/openssl-output/arm64-v8a/lib"
ENV OPENSSL_INC_DIR="/home/jenkins/openssl-output/arm64-v8a/include"

# Install Nim and clean up choosenim downloads
RUN curl https://nim-lang.org/choosenim/init.sh -sSf | sh -s -- -y \
 && /home/jenkins/.nimble/bin/choosenim ${NIM_VERSION} \
 && rm -rf /home/jenkins/.choosenim/downloads

# The Nimble bundled with Nim installs nim-sds without library/, so libsds
# cannot be built with it.
RUN set -eu; \
    case "$(dpkg --print-architecture)" in \
    amd64) NIMBLE_ARCH="linux_x64" ;; \
    arm64) NIMBLE_ARCH="linux_aarch64" ;; \
    *) echo "ERROR: unsupported architecture" >&2; exit 1 ;; \
    esac; \
    curl -sSfL -o /tmp/nimble.tar.gz \
      "https://github.com/nim-lang/nimble/releases/download/v${NIMBLE_VERSION}/nimble-${NIMBLE_ARCH}.tar.gz" \
 && tar -xzf /tmp/nimble.tar.gz -C /home/jenkins/.choosenim/toolchains/nim-${NIM_VERSION}/bin \
 && rm /tmp/nimble.tar.gz

# Install Nix and clean up Nix store cache
RUN curl -s https://nixos.org/releases/nix/nix-${NIX_VERSION}/install | sh -s -- --no-daemon \
 && rm -rf /home/jenkins/.cache/nix

# =============================================================================
# Switch back to root for final configuration
# =============================================================================
USER root

# Create symlinks for Nim tools
RUN ln -sf /home/jenkins/.choosenim/toolchains/nim-${NIM_VERSION}/bin/nim /usr/local/bin/nim \
 && ln -sf /home/jenkins/.choosenim/toolchains/nim-${NIM_VERSION}/bin/nimble /usr/local/bin/nimble \
 && ln -sf /home/jenkins/.choosenim/toolchains/nim-${NIM_VERSION}/bin/choosenim /usr/local/bin/choosenim \
 && chmod 755 /home/jenkins/.choosenim/toolchains/nim-${NIM_VERSION}/bin/* \
 && chown -R root:root /home/jenkins/.choosenim/toolchains/nim-${NIM_VERSION}/bin

# Set GCC alternatives
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
    --slave /usr/bin/g++ g++ /usr/bin/g++-13

# =============================================================================
# Final PATH and environment
# =============================================================================
ENV PATH="/opt/qt/${QT_VERSION}/android_arm64_v8a/bin:/opt/qt/${QT_VERSION}/gcc_64/bin:/opt/qt/${QT_VERSION}/gcc_64/libexec:/usr/local/go/bin:/home/jenkins/go/bin:/home/jenkins/.nimble/bin:/home/jenkins/.nix-profile/bin:/opt/android-sdk/cmdline-tools/latest/bin:/opt/android-sdk/platform-tools:/usr/local/bin:${PATH}"
ENV PKG_CONFIG_PATH="/opt/qt/${QT_VERSION}/android_arm64_v8a/lib/pkgconfig"
ENV ARCH=arm64

USER jenkins
WORKDIR /home/jenkins

ENTRYPOINT ["/bin/bash", "-l", "-c"]

LABEL maintainer="siddarth@status.im"
LABEL source="https://github.com/status-im/status-app"
LABEL description="Build image for the Status Android App"
