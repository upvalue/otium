{ pkgs ? import <nixpkgs> {}
, testMode ? null  # can be "hello", "mem", or "alternate"
}:

let
  kernelProg = if testMode == "hello" then "KERNEL_PROG_TEST_HELLO"
               else if testMode == "mem" then "KERNEL_PROG_TEST_MEM"
               else if testMode == "alternate" then "KERNEL_PROG_TEST_ALTERNATE"
               else "KERNEL_PROG_SHELL";

  testDescription = if testMode != null then " (test mode: ${testMode})" else "";
in

pkgs.stdenv.mkDerivation {
  pname = "otium-os-wasm";
  version = "0.1.0";

  src = ./.;

  nativeBuildInputs = [ pkgs.emscripten ];

  buildPhase = ''
    # Generate ot/config.h
    mkdir -p ot
    cat > ot/config.h <<EOF
#ifndef OT_CONFIG_H
#define OT_CONFIG_H

// Available kernel program modes
#define KERNEL_PROG_SHELL 0
#define KERNEL_PROG_TEST_HELLO 1
#define KERNEL_PROG_TEST_MEM 2
#define KERNEL_PROG_TEST_ALTERNATE 3

// Selected kernel program
#define KERNEL_PROG ${kernelProg}

#endif
EOF

    echo "Compiler: $(which emcc)"
    emcc --version | head -1

    # Compiler flags
    CPPFLAGS="-I. -DOT_ARCH_WASM -DOT_TRACE_MEM"
    CFLAGS="-O2 -g3 -Wall -Wextra -fno-exceptions -fno-rtti"

    # Emscripten-specific flags
    EMFLAGS=(
      -s ASYNCIFY=1
      -s INITIAL_MEMORY=67108864  # 64MB initial
      -s ALLOW_MEMORY_GROWTH=1
      -s "EXPORTED_FUNCTIONS=[_main]"
      -s "EXPORTED_RUNTIME_METHODS=[ccall,cwrap,print]"
      -s MODULARIZE=1
      -s "EXPORT_NAME=OtiumOS"
    )

    # Shared kernel source files (platform-independent)
    KERNEL_SHARED_SOURCES=(
      ot/kernel/startup.cpp
      ot/kernel/main.cpp
      ot/kernel/memory.cpp
      ot/kernel/process.cpp
      ot/shared/std.cpp
    )

    # Shared user program source files (platform-independent)
    USER_SHARED_SOURCES=(
      ot/user/prog-shell.cpp
      ot/user/user-shared.cpp
      ot/user/tcl.cpp
      ot/user/vendor/tlsf.c
    )

    # Build the complete system
    mkdir -p ot/kernel ot/user ot/shared
    emcc $CPPFLAGS $CFLAGS "''${EMFLAGS[@]}" -o ot/kernel/kernel.js \
      "''${KERNEL_SHARED_SOURCES[@]}" \
      ot/kernel/platform-wasm.cpp \
      ot/user/user-wasm.cpp \
      "''${USER_SHARED_SOURCES[@]}"

    echo ""
    echo "Build complete!"
  '';

  installPhase = ''
    mkdir -p $out/lib
    cp ot/kernel/kernel.js $out/lib/
    cp ot/kernel/kernel.wasm $out/lib/

    echo ""
    echo "Output files:"
    echo "  $out/lib/kernel.js"
    echo "  $out/lib/kernel.wasm"
  '';

  meta = with pkgs.lib; {
    description = "Otium OS compiled to WebAssembly";
    platforms = platforms.all;
  };
}
