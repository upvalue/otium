// run-wasm.js -- runs webassembly (under node)
const fs = require('fs');
const path = require('path');
const prompt = require('prompt-sync')({
    sigint: true,
    eot: true,
});

const wasmPath = path.join(__dirname, 'target/wasm32-unknown-unknown/debug/os.wasm');

async function runWasmOS() {
    
    // Load the WASM binary
    console.log(`Loading ${wasmPath}`);
    
    if (!fs.existsSync(wasmPath)) {
        console.error('WASM binary not found. Try running ./run-wasm.sh');
        process.exit(1);
    }
    
    const wasmBinary = fs.readFileSync(wasmPath);
    
    // Create imports for the WASM module
    const imports = {
        env: {
            host_exit: () => {
                process.exit(0);
            },
            host_print: (ptr, len) => {
                // Read string from WASM memory
                const bytes = new Uint8Array(wasmInstance.exports.memory.buffer, ptr, len);
                const str = new TextDecoder().decode(bytes);
                process.stdout.write(str);
            },
            host_readln: (buffer, bufferLength) => {
                const ln = prompt("");

                if(ln === null || ln === '') {
                    return 0;
                }
                
                const memory = wasmInstance.exports.memory;
                const memoryView = new Uint8Array(memory.buffer, buffer, bufferLength);

                const encoder = new TextEncoder();
                const encoded = encoder.encode(ln);
                const maxBytes = Math.min(encoded.length, bufferLength - 1);
                memoryView.set(encoded.subarray(0, maxBytes));
                memoryView[maxBytes] = 0;

                return 1;
            }
        }
    };
    
    // Instantiate the WASM module
    const wasmModule = await WebAssembly.compile(wasmBinary);
    const wasmInstance = await WebAssembly.instantiate(wasmModule, imports);

    console.log(`Running ${wasmPath}`)
    
    try {
        wasmInstance.exports.kernel_enter();
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