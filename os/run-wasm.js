#!/usr/bin/env node

// run-wasm.js - Node.js test runner for Otium OS WASM build
//
// Graphics Support:
// - Graphics runs in headless mode by default (operations complete, no visual output)
// - For visual graphics output with SDL window, install @kmamal/sdl:
//   npm install @kmamal/sdl
// - When enabled, graphics will render in a native SDL window


const fs = require('fs');
const path = require('path');
const readline = require('readline');

// Load the compiled WASM module
const OtiumOS = require('./bin/os.js');

// Check if we're in test mode
const isTestMode = process.env.OTIUM_TEST_MODE === '1';

// Read runtime config if it exists
let graphicsEnabled = true;  // Default to enabled for backward compatibility
try {
  const configPath = path.join(__dirname, 'build', 'runtime-config.json');
  const configData = JSON.parse(fs.readFileSync(configPath, 'utf8'));
  graphicsEnabled = configData.graphics.enabled;
} catch (e) {
  // Config file doesn't exist or can't be read, use default
}

// Input buffer for getchar
const inputBuffer = [];

// Readline interface (only created in interactive mode)
let rl = null;

// Buffer to accumulate output
let outputBuffer = '';


// Graphics setup for Node.js using SDL
// Note: We delay loading SDL until graphicsInit is called to avoid conflicts
let sdl = null;
let sdlAvailable = false;
let window = null;
let pixelBuffer = null;  // Reused buffer for BGRA->RGBA conversion

// Check if SDL is available without loading it yet
if (graphicsEnabled) {
  try {
    require.resolve('@kmamal/sdl');
    sdlAvailable = true;
    console.log('@kmamal/sdl found - graphics will be enabled');
  } catch (e) {
    console.log('@kmamal/sdl not installed - graphics will run in headless mode');
    console.log('To enable graphics: npm install @kmamal/sdl');
  }
} else {
  console.log('Graphics disabled by config - skipping SDL check');
}

// Module configuration
const Module = {
  // Graphics callbacks called from WASM via EM_JS
  graphicsInit: function(width, height) {
    console.log(`Graphics init: ${width}x${height}`);

    if (!sdlAvailable) {
      console.log('Graphics running in headless mode');
      return true; // Return true to allow headless operation
    }

    try {
      // Lazy load SDL now that we need it
      if (!sdl) {
        console.log('Loading @kmamal/sdl...');
        sdl = require('@kmamal/sdl');
      }

      // Create SDL window with RGBA format
      window = sdl.video.createWindow({
        title: 'Otium OS',
        width: width,
        height: height,
        resizable: false,
      });

      // Allocate reusable buffer for pixel format conversion
      pixelBuffer = Buffer.allocUnsafe(width * height * 4);

      console.log('SDL window created successfully');
      return true;
    } catch (e) {
      console.error('Failed to initialize graphics:', e);
      console.error('SDL graphics will be disabled. Continuing in headless mode.');
      sdlAvailable = false; // Disable for future calls
      return true; // Return true to continue in headless mode
    }
  },

  graphicsFlush: function(pixels, width, height) {
    if (!sdl || !window || !pixelBuffer) {
      // Headless mode - skip rendering
      return;
    }

    try {
      // pixels is a Uint32Array view into WASM memory (BGRA format)
      // SDL expects RGBA, so we need to convert
      // Reuse the pre-allocated buffer instead of allocating on every frame

      for (let i = 0; i < width * height; i++) {
        const pixel = pixels[i];
        const offset = i * 4;

        // WASM has BGRA (0xAARRGGBB), SDL wants RGBA
        pixelBuffer[offset + 0] = (pixel >> 16) & 0xFF; // R
        pixelBuffer[offset + 1] = (pixel >> 8) & 0xFF;  // G
        pixelBuffer[offset + 2] = pixel & 0xFF;         // B
        pixelBuffer[offset + 3] = (pixel >> 24) & 0xFF; // A
      }

      // Render to SDL window
      window.render(width, height, width * 4, 'rgba32', pixelBuffer);
    } catch (e) {
      // Check if window was destroyed (user closed it)
      if (e.message && e.message.includes('window is destroyed')) {
        console.log('\nWindow closed by user, exiting...');
        process.exit(0);
      }
      console.error('Graphics flush error:', e);
    }
  },

  graphicsCleanup: function() {
    console.log('Graphics cleanup');
    if (window) {
      window.destroy();
      window = null;
    }
    pixelBuffer = null;  // Release buffer reference
  },

  exit: (status) => {
    console.log('process exited with status', status);
    if (isTestMode) {
      // In test mode, exit immediately with the status
      process.exit(status || 0);
    } else {
      // In interactive mode, allow user to exit gracefully
      process.exit(0);
    }
  },

  time_get: function() {
    return Date.now();
  },

  print2: function(text) {
    process.stdout.write(text);
  },

  print3: function(text, size) {
    console.log({text, size});
  },

  printErr: function(text) {
    console.error('ERROR:', text);
  },

  // Input buffer accessed by ogetchar
  inputBuffer: inputBuffer,

  // Called when the runtime is ready
  onRuntimeInitialized: function() {
    if (isTestMode) {
      // Test mode: kernel runs and exits, no user interaction
      // main() is called automatically by Emscripten
    } else {
      // Interactive mode: set up readline for user input
      console.log('Otium OS WASM runtime initialized');
      console.log('Type commands and press Enter. Press Ctrl+C to exit.');
      console.log('');

      rl = readline.createInterface({
        input: process.stdin,
        output: process.stdout,
        terminal: true
      });

      // Set up stdin to read line by line
      rl.on('line', (line) => {
        // Add each character to the input buffer
        for (let i = 0; i < line.length; i++) {
          inputBuffer.push(line.charCodeAt(i));
        }
        // Add newline
        inputBuffer.push(13);
      });

      rl.on('close', () => {
        console.log('\nExiting...');
        process.exit(0);
      });
    }

    // Note: main() is called automatically by Emscripten since noInitialRun: false
  },

  onAbort: function(what) {
    console.error('Program aborted:', what);
    process.exit(1);
  },

  // Asyncify configuration
  noInitialRun: false,
  noExitRuntime: false,
};

// Create and run the module
OtiumOS(Module).then(instance => {
  // Module is initialized, onRuntimeInitialized will be called
}).catch(err => {
  console.error('Failed to load WASM module:', err);
  process.exit(1);
});
