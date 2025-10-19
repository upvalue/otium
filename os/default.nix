{ pkgs ? import <nixpkgs> {}
, testMode ? null  # can be "hello", "mem", or "alternate"
}:

let
  testFlag = if testMode == "hello" then "-DKERNEL_PROG_TEST_HELLO"
             else if testMode == "mem" then "-DKERNEL_PROG_TEST_MEM"
             else if testMode == "alternate" then "-DKERNEL_PROG_TEST_ALTERNATE"
             else "";

  testDescription = if testMode != null then " (test mode: ${testMode})" else "";
in

pkgs.stdenv.mkDerivation {
  pname = "otium-os-wasm";
  version = "0.1.0";

  src = ./.;

  nativeBuildInputs = [ pkgs.emscripten ];

  buildPhase = ''
    echo "Compiler: $(which emcc)"
    emcc --version | head -1

    # Compiler flags
    CPPFLAGS="-I. -DOT_ARCH_WASM -DOT_TRACE_MEM ${testFlag}"
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

    # Build the complete system
    emcc $CPPFLAGS $CFLAGS "''${EMFLAGS[@]}" -o otk/kernel.js \
      otk/kernel.cpp \
      otk/kernel-prog.cpp \
      otk/platform-wasm.cpp \
      otk/std.cpp \
      otk/memory.cpp \
      otk/process.cpp \
      otu/user-wasm.cpp \
      otu/prog-shell.cpp \
      otu/tcl.cpp \
      otu/vendor/tlsf.c

    echo ""
    echo "Build complete!"
  '';

  installPhase = ''
    mkdir -p $out/lib
    cp otk/kernel.js $out/lib/
    cp otk/kernel.wasm $out/lib/

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
