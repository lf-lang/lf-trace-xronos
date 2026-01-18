# LF Trace Plugin for the Xronos Dashboard

An LF trace plugin for displaying live traces on the Xronos Dashboard using OpenTelemetry.

## Overview

This plugin provides OpenTelemetry tracing support for Lingua Franca programs. It integrates with the Xronos Dashboard to display live traces and telemetry data.

## Building the Plugin

### Step 1: Build (choose one)

`build.sh` is a thin wrapper around CMake. Step 1 **configures + builds** (it does not install). Choose one:

#### 1a) Configure/build for the system prefix (default CMake prefix)

```bash
./build.sh
```

This produces:
- `lib/liblf-trace-impl.a`
- A build directory with cached dependencies under `build/_deps/`

#### 1b) Configure/build for a user-chosen prefix (install anywhere you like)

Choose any `PREFIX` directory you want. Installation will populate:
- `<PREFIX>/include`
- `<PREFIX>/lib`
- `<PREFIX>/lib/cmake/lf-trace-xronos`

Example (configure/build for this repo’s `./install` directory; this is what CI uses):

```bash
./build.sh --prefix "$(pwd)/install"
```

### Step 2: Install

Installing is a separate step (required to use the plugin from LF). Run this after Step 1:

```bash
./build.sh --install
```

(Use `sudo` only if your chosen prefix requires it.)

**Note:** Dependencies are cached in `build/_deps/` and won't be re-downloaded on subsequent builds unless you delete that directory.

## Step 3: Using the Plugin from Lingua Franca (three supported flows)

- If you chose **1a (system prefix)**, use **3a**.
- If you chose **1b (user-chosen prefix)**, use **3b** or **3c**.

### 3a) System package on default CMake search path (cleanest LF file)

Install the plugin to the system prefix (see above), then in your `.lf`:

```lf
target C {
  tracing: true, // Turn on tracing.
  trace-plugin: {
    package: "lf-trace-xronos",
    library: "lf::trace-impl",
  },
}
```

See a working example in `tests/src/TracePluginSystemPath.lf`.

### 3b) Package + explicit path (no system install; easy to remove)

Install the plugin to a user-chosen prefix (Step 1b + Step 2), then in your `.lf` set `path` to that prefix.

Example (install prefix is this repo’s `./install`, referenced relative to the `.lf` file):

```lf
target C {
  tracing: true, // Turn on tracing.
  trace-plugin: {
    package: "lf-trace-xronos",
    library: "lf::trace-impl",
    path: "../../install/", // Relative to this LF file's location
  },
}
```

See a working example in `tests/src/TracePluginUserPath.lf`.

### 3c) Custom CMake integration (most flexible; dev-focused)

Use `plugin.cmake` to link everything from an install directory and pass `LF_TRACE_INSTALL` (set to the Step 1b install prefix):

```lf
target C {
  tracing: true, // Turn on tracing.
  cmake-include: ["../../plugin.cmake"], // Relative to this LF file.
  cmake-args: {
    LF_TRACE_PLUGIN: "lf-trace-xronos",
    LF_TRACE_INSTALL: "../../../install", // Relative to the generated C project directory
  },
}
```

See a working example in `tests/src/TracePluginCustomCmake.lf`.

## End-to-end CI reference

For a complete working sequence (build lfc, install plugin both to `./install` and to system prefix, then compile+run the LF programs), see `.github/workflows/ci.yml`.

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
./build.sh [--log-level <n>] [--prefix <prefix>] [--install] [--clean]
```

- `--log-level <n>`: Set the log level (default: 4)
- `--prefix <prefix>`: Set `CMAKE_INSTALL_PREFIX` for the build (used by `--install`)
- `--install`: Install to the configured prefix
- `--clean`: Clear top-level CMake cache files (keeps `build/_deps/`)

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
   - Optionally installs headers, libraries, and CMake config via `./build.sh --install`
   - Dependencies are cached in `build/_deps/` for reuse

2. **LF program build stage** (`plugin.cmake`):
   - Finds the install directory (via `LF_TRACE_INSTALL` or auto-detection)
   - Links all libraries from `install/lib/`
   - Adds include directories from `install/include/`

This design allows:
- Building the plugin once and reusing it for multiple LF programs
- No re-downloading/rebuilding dependencies for each LF program
- Clean separation between plugin build and LF program build

