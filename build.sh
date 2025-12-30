#!/bin/bash

set -euo pipefail

# `trace/impl` is a standalone CMake project (and can also be included from the
# reactor-c top-level build). This script builds the standalone project and
# produces `lib/lf-trace-impl.a`, then creates an install directory.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
INSTALL_DIR="${SCRIPT_DIR}/install"  # Default to ./install

LOG_LEVEL="${LOG_LEVEL:-4}"
DO_CLEAN=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --install)
      INSTALL_DIR="$2"
      shift 2
      ;;
    --log-level)
      LOG_LEVEL="$2"
      shift 2
      ;;
    --no-clean)
      DO_CLEAN=0
      shift 1
      ;;
    -h|--help)
      cat <<'EOF'
Usage:
  ./build.sh [--log-level <n>] [--install <install-dir>] [--no-clean]

Builds lib/liblf-trace-impl.a and creates an install directory.

By default, creates a reusable install folder at ./install containing:
  install/include/
  install/lib/

This install directory is intended to be referenced by plugin.cmake via LF_TRACE_INSTALL.
You can override the location with --install <path>.

By default, this script clears only the top-level CMake cache files (keeps build/_deps/).
Use --no-clean to skip cache clearing.
EOF
      exit 0
      ;;
    *)
      echo "Unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

# Clean step
if [[ "${DO_CLEAN}" -eq 1 ]]; then
  echo "Removing CMake cache and generated files..."
  rm -f "${BUILD_DIR}/CMakeCache.txt"
  rm -rf "${BUILD_DIR}/CMakeFiles/"
  rm -f "${SCRIPT_DIR}/lib/liblf-trace-impl.a"
  echo "Cache cleared. Dependencies preserved in ${BUILD_DIR}/_deps/"
fi

# Build step
echo "Building lf-trace-impl..."
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DLOG_LEVEL="${LOG_LEVEL}" --no-warn-unused-cli
cmake --build "${BUILD_DIR}" -j8 --target lf-trace-impl

# Install step - create install directory and copy files
INSTALL_DIR="$(mkdir -p "${INSTALL_DIR}" && cd "${INSTALL_DIR}" && pwd)"
echo "Creating install directory at: ${INSTALL_DIR}"

mkdir -p "${INSTALL_DIR}/include" "${INSTALL_DIR}/lib"

echo "Copying headers..."
# Plugin public headers
cp -R "${SCRIPT_DIR}/include/"* "${INSTALL_DIR}/include/" 2>/dev/null || true
# opentelemetry-c public headers (needed because trace_impl.c includes opentelemetry_c/opentelemetry_c.h)
cp -R "${SCRIPT_DIR}/third-party/opentelemetry-c/include/"* "${INSTALL_DIR}/include/" 2>/dev/null || true

echo "Copying static libraries..."

# Main plugin library (built by this repo)
if [[ -f "${SCRIPT_DIR}/lib/liblf-trace-impl.a" ]]; then
  cp "${SCRIPT_DIR}/lib/liblf-trace-impl.a" "${INSTALL_DIR}/lib/"
else
  echo "ERROR: ${SCRIPT_DIR}/lib/liblf-trace-impl.a not found. Build failed?" >&2
  exit 1
fi

# opentelemetry-c wrapper library built in this repo build tree
if [[ -f "${BUILD_DIR}/third-party/opentelemetry-c/libopentelemetry-c.a" ]]; then
  cp "${BUILD_DIR}/third-party/opentelemetry-c/libopentelemetry-c.a" "${INSTALL_DIR}/lib/"
else
  # fallback (depending on generator / build layout)
  found_otelc="$(find "${BUILD_DIR}" -maxdepth 4 -name 'libopentelemetry-c.a' -type f | head -n 1 || true)"
  if [[ -n "${found_otelc}" ]]; then
    cp "${found_otelc}" "${INSTALL_DIR}/lib/"
  else
    echo "WARNING: libopentelemetry-c.a not found under build/. Your LF program link will fail unless it's present." >&2
  fi
fi

# opentelemetry-cpp static libs
if [[ -d "${BUILD_DIR}/_deps/opentelemetry-cpp-build" ]]; then
  find "${BUILD_DIR}/_deps/opentelemetry-cpp-build" -name '*.a' -type f -maxdepth 6 -print0 \
    | xargs -0 -I{} cp "{}" "${INSTALL_DIR}/lib/" || true
fi

# gRPC + bundled deps (upb, utf8_range_lib, ssl, crypto, c-ares, etc)
if [[ -d "${BUILD_DIR}/_deps/grpc-build" ]]; then
  find "${BUILD_DIR}/_deps/grpc-build" -name 'lib*.a' -type f -maxdepth 3 -print0 \
    | xargs -0 -I{} cp "{}" "${INSTALL_DIR}/lib/" || true
fi

# c-ares library (DNS resolution, might be in grpc-build or separate)
if [[ -d "${BUILD_DIR}/_deps/grpc-build" ]]; then
  find "${BUILD_DIR}/_deps/grpc-build" -name 'libcares*.a' -type f -maxdepth 6 -print0 \
    | xargs -0 -I{} cp "{}" "${INSTALL_DIR}/lib/" || true
fi

# Protobuf static libs (libprotobuf*.a)
if [[ -d "${BUILD_DIR}/_deps/protobuf-build" ]]; then
  find "${BUILD_DIR}/_deps/protobuf-build" -name 'libprotobuf*.a' -type f -maxdepth 6 -print0 \
    | xargs -0 -I{} cp "{}" "${INSTALL_DIR}/lib/" || true
fi

# Abseil static libs (libabsl_*.a)
if [[ -d "${BUILD_DIR}/_deps/absl-build" ]]; then
  find "${BUILD_DIR}/_deps/absl-build" -name 'libabsl_*.a' -type f -maxdepth 6 -print0 \
    | xargs -0 -I{} cp "{}" "${INSTALL_DIR}/lib/" || true
fi

echo "Install directory contents:"
echo "  include/: $(find "${INSTALL_DIR}/include" -type f | wc -l | tr -d ' ') files"
echo "  lib/:     $(find "${INSTALL_DIR}/lib" -name '*.a' -type f | wc -l | tr -d ' ') archives"

echo "Build and install complete!"
