// run-wasm.js -- runs webassembly (under node)
const fs = require('fs');
const path = require('path');

async function runWasmOS() {
    console.log('Loading WASM Rust OS...');
    
    // Load the WASM binary
    const wasmPath = path.join(__dirname, 'target/wasm32-unknown-unknown/debug/os.wasm');
    
    if (!fs.existsSync(wasmPath)) {
        console.error('WASM binary not found. Please run ./build.sh first');
        process.exit(1);
    }
    
    const wasmBinary = fs.readFileSync(wasmPath);
    
    // Create imports for the WASM module
    const imports = {
        env: {
            host_print: (ptr, len) => {
                // Read string from WASM memory
                const bytes = new Uint8Array(wasmInstance.exports.memory.buffer, ptr, len);
                const str = new TextDecoder().decode(bytes);
                process.stdout.write(str);
            }
        }
    };
    
    // Instantiate the WASM module
    const wasmModule = await WebAssembly.compile(wasmBinary);
    const wasmInstance = await WebAssembly.instantiate(wasmModule, imports);

    console.log(`Running ${wasmPath}`)
    
    try {
        wasmInstance.exports.start();
    } catch(e) {
        console.error(`Exception ${e.toString()}`);
    }

    console.log('Program exited');
}

// Check if Node.js supports WebAssembly
if (typeof WebAssembly !== 'object') {
    console.error('WebAssembly not supported in this environment');
    process.exit(1);
}

runWasmOS().catch(error => {
    console.error('Error running WASM OS:', error);
    process.exit(1);
});