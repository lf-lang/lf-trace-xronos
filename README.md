# lf-trace-xronos

An LF trace plugin for displaying live traces on the Xronos Dashboard.

**Note:** This repository currently contains the default trace plugin implementation from the reactor-c repository. We plan to update this repository to support OpenTelemetry and the Xronos Dashboard.

## Usage

### Step 1: Compile and build the library

Run the build script:

```bash
./build.sh
```

This will produce `lib/liblf-trace-impl.a`.

### Step 2: Copy the library to the same directory as your LF program

Copy `lib/liblf-trace-impl.a` to the directory containing your LF program that you want to link against.

For example, if your LF program is located at `/path/to/my/program/MyProgram.lf`:

```bash
cp lib/liblf-trace-impl.a /path/to/my/program/
```

### Step 3: Modify your LF program

Add the following configuration to your LF program's target:

```lf
target C {
  tracing: true,
  trace-plugin: lf-trace-impl,
  files: "liblf-trace-impl.a", // relative to the LF file, not the root of the LF compiler.
}
```

### Step 4: Compile and run the LF program

Compile your LF program with tracing enabled:

```bash
lfc-dev -c /path/to/my/program/MyProgram.lf --tracing
```

The trace plugin will be linked automatically.
