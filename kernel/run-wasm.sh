#!/bin/bash
set -xue

export RUSTFLAGS=""
cargo build --target wasm32-unknown-unknown

node run-wasm.js
