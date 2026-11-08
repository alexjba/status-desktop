#!/usr/bin/env bash
# Builds libStatusReplay (ui/replay) for the mobile target against the mobile libStatusQ.
set -ef pipefail

BASEDIR=$(dirname "$0")

source "${BASEDIR}/commonCmakeConfig.sh"

STATUS_DESKTOP=${STATUS_DESKTOP:="../vendors/status-desktop"}
STATUS_REPLAY="${STATUS_DESKTOP}/ui/replay"
LIB_DIR=${LIB_DIR}
LIB_EXT=${LIB_EXT:=".so"}
STATUS_Q_LIB=${STATUS_Q_LIB}

BUILD_DIR="${STATUS_REPLAY}/build/${OS}"

cmake -S "${STATUS_REPLAY}" -B "${BUILD_DIR}" \
    "${COMMON_CMAKE_CONFIG[@]}" \
    -DSTATUSREPLAY_BUILD_TESTS=OFF \
    -DSTATUSQ_LIBRARY="${STATUS_Q_LIB}"

make -C "${BUILD_DIR}" StatusReplay -j "$(nproc)"

mkdir -p "${LIB_DIR}"
cp "$(find "${BUILD_DIR}" -name "libStatusReplay${LIB_EXT}")" "${LIB_DIR}/libStatusReplay${LIB_EXT}"
