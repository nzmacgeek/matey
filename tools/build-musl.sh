#!/usr/bin/env bash
# tools/build-musl.sh — clone nzmacgeek/musl-blueyos and build it for i386
#
# Usage:
#   ./tools/build-musl.sh [--prefix=<path>]
#
# Variables:
#   PREFIX        - install prefix (default: build/musl)
#   TARGET        - musl target triplet (default: i386-linux-musl)
#   MUSL_REPO     - GitHub repo to clone (default: nzmacgeek/musl-blueyos)
#
# After this script completes, build matey with:
#   make MUSL_PREFIX=<PREFIX>
#
# ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PREFIX="${REPO_DIR}/build/musl"
TARGET="${TARGET:-i386-linux-musl}"
MUSL_REPO="${MUSL_REPO:-nzmacgeek/musl-blueyos}"
BUILD_TMP="${BUILD_TMP:-/tmp/matey-musl-build}"

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix=*)  PREFIX="${1#*=}";  shift ;;
    --target=*)  TARGET="${1#*=}";  shift ;;
    --repo=*)    MUSL_REPO="${1#*=}"; shift ;;
    --help|-h)
      sed -n '/^# Usage:/,/^[^#]/{ /^[^#]/d; s/^# \{0,1\}//; p }' "$0"
      exit 0
      ;;
    *) echo "Unknown argument: $1" >&2; exit 1 ;;
  esac
done

MUSL_CLONE_URL="https://github.com/${MUSL_REPO}.git"
MUSL_SRC_DIR="${BUILD_TMP}/musl-blueyos"

echo "Building musl-blueyos for ${TARGET}"
echo "  source : ${MUSL_CLONE_URL}"
echo "  prefix : ${PREFIX}"
echo "  workdir: ${BUILD_TMP}"
echo ""

mkdir -p "${BUILD_TMP}"

# ---------------------------------------------------------------------------
# Clone / update the musl-blueyos source
# ---------------------------------------------------------------------------
if [ -d "${MUSL_SRC_DIR}/.git" ]; then
  echo "Updating existing clone at ${MUSL_SRC_DIR}..."
  git -C "${MUSL_SRC_DIR}" fetch origin main
  git -C "${MUSL_SRC_DIR}" reset --hard origin/main
else
  echo "Cloning ${MUSL_CLONE_URL}..."
  git clone --depth=1 "${MUSL_CLONE_URL}" "${MUSL_SRC_DIR}"
fi

cd "${MUSL_SRC_DIR}"

# ---------------------------------------------------------------------------
# Configure
#
# We use the host gcc with -m32 to produce an i386 static library.
# This mirrors how the biscuits project builds its musl sysroot for i386.
# ---------------------------------------------------------------------------
CC="${CC:-gcc}"

./configure \
  --prefix="${PREFIX}" \
  --target="${TARGET}" \
  CC="${CC}" \
  CFLAGS="-m32 -O2" \
  LDFLAGS="-m32"

# ---------------------------------------------------------------------------
# Build and install
# ---------------------------------------------------------------------------
make -j"$(nproc)"
make install

echo ""
echo "  musl-blueyos installed to: ${PREFIX}"
echo ""
echo "  Build matey now with:"
echo "    make MUSL_PREFIX=${PREFIX}"
