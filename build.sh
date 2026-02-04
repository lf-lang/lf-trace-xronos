#!/bin/bash

set -euo pipefail

# `trace/impl` is a standalone CMake project (and can also be included from the
# reactor-c top-level build). This script builds the standalone project and
# produces `lib/lf-trace-impl.a`, then creates an install directory.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
PREFIX=""  # Build-time CMAKE_INSTALL_PREFIX (set via --prefix). If empty, use CMake's default.

LOG_LEVEL="${LOG_LEVEL:-4}"
DO_CLEAN=0
DO_INSTALL=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --install)
      DO_INSTALL=1
      shift 1
      ;;
    --prefix)
      PREFIX="$2"
      shift 2
      ;;
    --prefix=*)
      PREFIX="${1#--prefix=}"
      shift 1
      ;;
    --log-level)
      LOG_LEVEL="$2"
      shift 2
      ;;
    --clean)
      DO_CLEAN=1
      shift 1
      ;;
    -h|--help)
      cat <<'EOF'
Usage:
  # Build (configure + build). Does NOT install:
  ./build.sh [--log-level <n>] [--prefix <prefix> | --prefix=<prefix>] [--clean]

  # Install-only (no configure/build). Requires a prior build:
  ./build.sh --install

  # Clean dependencies only:
  ./build.sh --clean

Options:
  --prefix <prefix>: set CMAKE_INSTALL_PREFIX at configure/build time.
  --install: install-only to the configured CMAKE_INSTALL_PREFIX (no rebuild; like `make install`).
  --clean: clears cached third-party dependencies in build/_deps/.

Notes:
  - Installing to the default prefix may require admin privileges (e.g., sudo) depending on your system.

EOF
      exit 0
      ;;
    *)
      echo "Unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

# Install-only mode (mirrors `make install` / `cmake --install`):
# - Do not configure or rebuild.
# - Require an existing configured build directory and built artifact.
if [[ "${DO_INSTALL}" -eq 1 ]]; then
  artifact="${SCRIPT_DIR}/lib/liblf-trace-impl.a"
  if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "ERROR: ${BUILD_DIR}/CMakeCache.txt not found. Run ./build.sh first to configure/build, then rerun with --install." >&2
    exit 1
  fi
  if [[ ! -f "${artifact}" ]]; then
    echo "ERROR: ${artifact} not found. Run ./build.sh first to build, then rerun with --install." >&2
    exit 1
  fi

  resolved_prefix="$(sed -n 's/^CMAKE_INSTALL_PREFIX:PATH=//p' "${BUILD_DIR}/CMakeCache.txt" | tail -n 1)"

  echo "Install prefix: ${resolved_prefix}"
  echo "Installing to: ${resolved_prefix}"
  cmake --install "${BUILD_DIR}"
  exit 0
fi

# Clean step
if [[ "${DO_CLEAN}" -eq 1 ]]; then
  rm -rf "${BUILD_DIR}/_deps/"
  echo "All dependencies cleaned."
  exit 0
fi

# Make sure cache is cleaned before building
# so that the install path gets updated for each build.
echo "Removing CMake cache and generated files from previous builds..."
rm -f "${BUILD_DIR}/CMakeCache.txt"
rm -rf "${BUILD_DIR}/CMakeFiles/"
rm -f "${SCRIPT_DIR}/lib/liblf-trace-impl.a"

echo "Building lf-trace-impl..."
cmake_args=(
  -S "${SCRIPT_DIR}"
  -B "${BUILD_DIR}"
  -DLOG_LEVEL="${LOG_LEVEL}"
  --no-warn-unused-cli
)

if [[ -n "${PREFIX}" ]]; then
  PREFIX="$(mkdir -p "${PREFIX}" && cd "${PREFIX}" && pwd)"
  cmake_args+=(-DCMAKE_INSTALL_PREFIX="${PREFIX}")
fi

cmake "${cmake_args[@]}"

cmake --build "${BUILD_DIR}" -j8 --target lf-trace-impl
echo "Build complete!"

resolved_prefix="$(sed -n 's/^CMAKE_INSTALL_PREFIX:PATH=//p' "${BUILD_DIR}/CMakeCache.txt" | tail -n 1)"
echo "Next, run the following to install (you may need sudo): ./build.sh --install"
echo "Plugin installation path: ${resolved_prefix}"
