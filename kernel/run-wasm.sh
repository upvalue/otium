#!/bin/bash
set -xue

export RUSTFLAGS="--cfg kernel_prog=\"echo\""
cargo build --target wasm32-unknown-unknown

node run-wasm.js
