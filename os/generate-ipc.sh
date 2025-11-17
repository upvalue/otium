#!/bin/bash
set -euo pipefail

echo "Generating IPC code from services.yaml..."

# Run code generation
deno run --allow-read --allow-write tools/ipc-codegen/main.ts \
  --input services.yaml \
  --output ot/user/gen/

echo ""
echo "Done! Generated files are in ot/user/gen/"
echo "Implementation stubs are in ot/user/<service_name>/impl.cpp"
echo ""
echo "Next steps:"
echo "  1. Implement the service methods in ot/user/<service>/impl.cpp"
echo "  2. Run ./compile-riscv.sh or ./compile-wasm.sh to build"
