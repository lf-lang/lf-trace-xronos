# LF Trace Plugin for the Xronos Dashboard

![Demo dashboard screenshot](img/demo.png)

An LF trace plugin for displaying live traces on the Xronos Dashboard using OpenTelemetry.

## Overview

This plugin provides OpenTelemetry tracing support for Lingua Franca programs. It integrates with the Xronos Dashboard to display live traces and telemetry data.

## Building the Plugin

<a id="step-1-clone"></a>
### Step 1: Clone the repository with submodules

```bash
git clone --recurse-submodules https://github.com/lf-lang/lf-trace-xronos.git
cd lf-trace-xronos
```

<a id="step-2-dashboard"></a>
### Step 2: Install the Xronos Dashboard

```bash
pip install xronos-dashboard
```

Please see [here](https://docs.xronos.com/dashboard.html#installation) for more information.

<a id="step-3-build"></a>
### Step 3: Build (choose one)

<a id="option-a"></a>
#### Option A) Build for the system path (e.g., `/usr/local/`)

```bash
./build.sh
```

This option gives you the *cleanest* LF programs later (see [5a](#5a)).

<a id="option-b"></a>
#### Option B) Build for a custom path (install anywhere you like)

Example - build for an `install/` directory within the repo:

```bash
./build.sh --prefix "$(pwd)/install"
```

<a id="step-4-install"></a>
### Step 4: Install

Installing is a separate step (required to use the plugin from LF). Run this after [Step 3](#step-3-build):

```bash
./build.sh --install
```

(Use `sudo` only if your chosen prefix requires it.)

<a id="step-5-using"></a>
## Step 5: Using the Plugin from Lingua Franca (three supported flows)

- If you chose **[Option A](#option-a)** (system prefix), use **[5a](#5a)**.
- If you chose **[Option B](#option-b)** (user-chosen prefix), use **[5b](#5b)** or **[5c](#5c)**.

<a id="5a"></a>
### 5a) System package on default CMake search path (cleanest LF file)

Install the plugin to the system prefix, then in your `.lf`:

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

<a id="5b"></a>
### 5b) Package + explicit path (no system install; easy to remove)

Install the plugin to a user-chosen prefix ([Option B](#option-b) + [Step 4](#step-4-install)), then in your `.lf` set `path` to that prefix.

Example (install prefix is this repo’s `install/`, referenced relative to the `.lf` file):

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

<a id="5c"></a>
### 5c) Custom CMake integration (most flexible; dev-focused)

Use `plugin.cmake` to link everything from an install directory and pass `LF_TRACE_INSTALL` (set to the [Option B](#option-b) install prefix):

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

