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
    # Fix shebangs for Nix environment
    patchShebangs config.sh compile-wasm.sh

    # Generate ot/config.h using config.sh with WASM backend (no SDL2 dependency)
    ${if testMode != null
      then "./config.sh --test-${testMode} --graphics-backend=wasm"
      else "./config.sh --graphics-backend=wasm"}

    # Build using compile-wasm.sh
    ./compile-wasm.sh
  '';

  installPhase = ''
    mkdir -p $out/lib
    cp bin/os.js $out/lib/
    cp bin/os.wasm $out/lib/

    echo ""
    echo "Output files:"
    echo "  $out/lib/os.js"
    echo "  $out/lib/os.wasm"
  '';

  meta = with pkgs.lib; {
    description = "Otium OS compiled to WebAssembly";
    platforms = platforms.all;
  };
}
