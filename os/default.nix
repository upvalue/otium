{ pkgs ? import <nixpkgs> {}
, testMode ? null  # can be "hello", "mem", or "alternate"
}:

let
  # TODO: When Meson supports configuration options, use them here
  # For now, testMode is not used - all builds use default KERNEL_PROG_SHELL
  testDescription = if testMode != null then " (test mode: ${testMode})" else "";
in

pkgs.stdenv.mkDerivation {
  pname = "otium-os-wasm";
  version = "0.1.0";

  src = ./.;

  nativeBuildInputs = with pkgs; [
    meson
    ninja
    emscripten
  ];

  configurePhase = ''
    meson setup build-wasm --cross-file=cross/wasm.txt
  '';

  buildPhase = ''
    meson compile -C build-wasm -v
  '';

  installPhase = ''
    mkdir -p $out/lib
    cp build-wasm/os.js $out/lib/
    cp build-wasm/os.wasm $out/lib/

    echo ""
    echo "Output files:"
    echo "  $out/lib/os.js"
    echo "  $out/lib/os.wasm"
  '';

  meta = with pkgs.lib; {
    description = "Otium OS compiled to WebAssembly using Meson";
    platforms = platforms.all;
  };
}
