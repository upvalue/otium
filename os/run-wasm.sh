#!/bin/bash
set -xue

cargo build --target wasm32-unknown-unknown

node run-wasm.js
