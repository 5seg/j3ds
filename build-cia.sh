#!/usr/bin/env bash
set -euo pipefail

J3DS_ROOT="/home/master/workspace/j3ds"
BUILD_DIR="/tmp/j3ds-build"

echo "==> Building Jellyfin 3DS (.cia) in container j3ds-dev..."
incus exec j3ds-dev -- bash -lc "
  rm -rf ${BUILD_DIR}
  mkdir -p ${BUILD_DIR}
  cd /workspace/j3ds
  make -j\$(nproc) BUILD=${BUILD_DIR} OUTPUT=${BUILD_DIR}/j3ds cia
"

echo "==> Pulling .cia artifact to host..."
incus file pull "j3ds-dev${BUILD_DIR}/j3ds.cia" "${J3DS_ROOT}/j3ds.cia"

echo "==> Done. Artifact:"
ls -lh "${J3DS_ROOT}/j3ds.cia"
