# lf-trace-xronos

An LF trace plugin for displaying live traces on the Xronos Dashboard using OpenTelemetry.

## Overview

This plugin provides OpenTelemetry tracing support for Lingua Franca programs. It integrates with the Xronos Dashboard to display live traces and telemetry data.

## Building the Plugin

### Step 1: Build the plugin and create install directory

Run the build script:

```bash
./build.sh
```

This will:
- Build `lib/liblf-trace-impl.a` and all dependencies
- Create an `install/` directory containing:
  - `install/include/` - All required headers
  - `install/lib/` - All static libraries (113+ archives including OpenTelemetry, gRPC, Protobuf, Abseil, etc.)

The install directory is created at `./install` by default. You can specify a custom location:

```bash
./build.sh --install /custom/path/to/install
```

**Note:** Dependencies are cached in `build/_deps/` and won't be re-downloaded on subsequent builds unless you delete that directory.

### Step 2: Use the plugin in your LF program

#### Option A: Auto-detection (recommended)

If `plugin.cmake` is in the same directory as your LF program's `install/` directory, it will auto-detect:

```lf
target C {
  tracing: true,
  cmake-include: ["plugin.cmake"],
}
```

#### Option B: Specify install directory explicitly

Set the install directory via CMake variable:

```bash
lfc-dev -c MyProgram.lf --tracing --cmake-define LF_TRACE_INSTALL=/abs/path/to/lf-trace-xronos/install
```

Or via environment variable:

```bash
export LF_TRACE_INSTALL=/abs/path/to/lf-trace-xronos/install
lfc-dev -c MyProgram.lf --tracing
```

#### Option C: Copy plugin.cmake to your project

Copy `plugin.cmake` to your LF program directory and reference it:

```lf
target C {
  tracing: true,
  cmake-include: ["path/to/plugin.cmake"],
}
```

Then set `LF_TRACE_INSTALL` as described in Option B.

## What plugin.cmake Does

The `plugin.cmake` file automatically:
- Links all required static libraries from the install directory
- Adds include directories for headers
- Links system libraries (zlib, pthread, C++ standard library, CoreFoundation on macOS, c-ares)
- Sets appropriate C++ standard (C++14)

## Dependencies

The plugin depends on:
- **OpenTelemetry C++ SDK** (v1.8.1+) - Core tracing functionality
- **OpenTelemetry C wrapper** - C API wrapper around the C++ SDK
- **gRPC** - For OTLP export
- **Protobuf** - Protocol buffer serialization
- **Abseil** - C++ utilities
- **c-ares** - DNS resolution (for gRPC)
- **System libraries**: zlib, pthread, C++ standard library, CoreFoundation (macOS)

All dependencies are built as static libraries and included in the `install/` directory.

## Build Options

The `build.sh` script supports several options:

```bash
./build.sh [--log-level <n>] [--install <install-dir>] [--no-clean]
```

- `--log-level <n>`: Set the log level (default: 4)
- `--install <install-dir>`: Specify custom install directory (default: `./install`)
- `--no-clean`: Skip cleaning CMake cache (faster for incremental builds)

## Troubleshooting

### Missing symbols at link time

If you encounter linker errors for missing symbols:

1. **CoreFoundation symbols** (macOS): Should be automatically linked by `plugin.cmake`
2. **c-ares symbols**: Check if c-ares is installed system-wide or ensure it's in the install directory
3. **Other missing symbols**: Ensure `./build.sh` completed successfully and the install directory contains all libraries

### Rebuilding dependencies

To force a complete rebuild (including re-downloading dependencies):

```bash
rm -rf build/_deps/
./build.sh
```

## Architecture

The plugin uses a two-stage build process:

1. **Plugin build stage** (`./build.sh`):
   - Builds the plugin library and all dependencies
   - Copies headers and libraries to `install/` directory
   - Dependencies are cached in `build/_deps/` for reuse

2. **LF program build stage** (`plugin.cmake`):
   - Finds the install directory (via `LF_TRACE_INSTALL` or auto-detection)
   - Links all libraries from `install/lib/`
   - Adds include directories from `install/include/`

This design allows:
- Building the plugin once and reusing it for multiple LF programs
- No re-downloading/rebuilding dependencies for each LF program
- Clean separation between plugin build and LF program build

