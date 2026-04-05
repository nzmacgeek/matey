#!/usr/bin/env bash
# tools/build-musl.sh — download and build musl libc for i386 (BlueyOS target)
#
# Usage:
#   ./tools/build-musl.sh [--prefix=<path>] [--musl-version=<version>]
#
# Variables:
#   PREFIX        - install prefix (default: build/musl)
#   TARGET        - musl target triplet (default: i386-linux-musl)
#   MUSL_VERSION  - musl release to fetch (default: 1.2.4)
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
MUSL_VERSION="${MUSL_VERSION:-1.2.4}"
MUSL_TARBALL="musl-${MUSL_VERSION}.tar.gz"
MUSL_URL="https://musl.libc.org/releases/${MUSL_TARBALL}"
BUILD_TMP="${BUILD_TMP:-/tmp/matey-musl-build}"

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix=*)       PREFIX="${1#*=}";       shift ;;
    --target=*)       TARGET="${1#*=}";       shift ;;
    --musl-version=*) MUSL_VERSION="${1#*=}"; shift ;;
    --help|-h)
      sed -n '/^# Usage:/,/^[^#]/{ /^[^#]/d; s/^# \{0,1\}//; p }' "$0"
      exit 0
      ;;
    *) echo "Unknown argument: $1" >&2; exit 1 ;;
  esac
done

echo "Building musl ${MUSL_VERSION} for ${TARGET}"
echo "  prefix : ${PREFIX}"
echo "  workdir: ${BUILD_TMP}"
echo ""

mkdir -p "${BUILD_TMP}"
cd "${BUILD_TMP}"

# ---------------------------------------------------------------------------
# Download
# ---------------------------------------------------------------------------
if [ ! -f "${MUSL_TARBALL}" ]; then
  echo "Downloading musl ${MUSL_VERSION}..."
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "${MUSL_URL}" -o "${MUSL_TARBALL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -q "${MUSL_URL}" -O "${MUSL_TARBALL}"
  else
    echo "Error: neither curl nor wget found." >&2
    exit 1
  fi
fi

# ---------------------------------------------------------------------------
# Extract
# ---------------------------------------------------------------------------
rm -rf "musl-${MUSL_VERSION}"
tar xzf "${MUSL_TARBALL}"
cd "musl-${MUSL_VERSION}"

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
echo "  musl installed to: ${PREFIX}"
echo ""
echo "  Build matey now with:"
echo "    make MUSL_PREFIX=${PREFIX}"
