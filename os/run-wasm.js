#!/usr/bin/env node

// run-wasm.js - Node.js test runner for Otium OS WASM build
//
// Usage: node run-wasm.js [build-dir]
//
// Environment:
//   OTIUM_TEST_MODE=1  - Run in test mode (no interactive input)

const fs = require('fs');
const path = require('path');
const readline = require('readline');

const support = require('./wasm-support.js');

// Get build directory from command line args or use default
const buildDirArg = process.argv[2] || 'build-wasm';
const buildDir = path.isAbsolute(buildDirArg)
  ? buildDirArg
  : path.join(__dirname, buildDirArg);

// Load the compiled WASM module
const OtiumOS = require(path.join(buildDir, 'os.js'));

// Check if we're in test mode
const isTestMode = process.env.OTIUM_TEST_MODE === '1';

// Read runtime config if it exists
let graphicsEnabled = true;
try {
  const configPath = path.join(buildDir, 'runtime-config.json');
  const configData = JSON.parse(fs.readFileSync(configPath, 'utf8'));
  graphicsEnabled = configData.graphics.enabled;
} catch (e) {
  // Config file doesn't exist or can't be read, use default
}

// Load files from fs-in/ at startup
const fsInDir = path.join(__dirname, 'fs-in');
const loadedCount = support.loadFilesFromDir(fsInDir);
if (loadedCount > 0) {
  console.log(`Loaded ${loadedCount} entries from fs-in/`);
}

// Check SDL availability
if (graphicsEnabled) {
  if (support.checkSdlAvailable()) {
    console.log('@kmamal/sdl found - graphics will be enabled');
  } else {
    console.log('@kmamal/sdl not installed - graphics will run in headless mode');
    console.log('To enable graphics: npm install @kmamal/sdl');
  }
} else {
  console.log('Graphics disabled by config - skipping SDL check');
}

// Input buffer for getchar
const inputBuffer = [];

// Readline interface (only created in interactive mode)
let rl = null;

// Module configuration
const Module = {
  // Graphics callbacks
  graphicsInit: function(width, height) {
    console.log(`Graphics init: ${width}x${height}`);
    const result = support.graphicsInit(width, height);
    if (!result) {
      console.log('Graphics running in headless mode');
    }
    return true;
  },
  graphicsFlush: support.graphicsFlush,
  graphicsCleanup: function() {
    console.log('Graphics cleanup');
    support.graphicsCleanup();
  },

  // Keyboard callbacks
  keyboardInit: support.keyboardInit,
  keyboardPoll: support.keyboardPoll,
  keyboardCleanup: support.keyboardCleanup,

  // Filesystem callbacks
  ...support.filesystemCallbacks,

  // Exit handler
  exit: (status) => {
    const fsOutDir = path.join(__dirname, 'fs-out');
    const savedCount = support.saveFilesToDir(fsOutDir);
    console.log(`Saved ${savedCount} entries to fs-out/`);
    console.log('process exited with status', status);
    process.exit(isTestMode ? (status || 0) : 0);
  },

  // Time
  time_get: function() {
    return Date.now();
  },

  // Output
  print2: function(text) {
    process.stdout.write(text);
  },
  printErr: function(text) {
    console.error('ERROR:', text);
  },

  // Input buffer accessed by ogetchar
  inputBuffer: inputBuffer,

  // Called when the runtime is ready
  onRuntimeInitialized: function() {
    if (!isTestMode) {
      console.log('Otium OS WASM runtime initialized');
      console.log('Type commands and press Enter. Press Ctrl+C to exit.');
      console.log('');

      rl = readline.createInterface({
        input: process.stdin,
        output: process.stdout,
        terminal: true
      });

      rl.on('line', (line) => {
        for (let i = 0; i < line.length; i++) {
          inputBuffer.push(line.charCodeAt(i));
        }
        inputBuffer.push(13);
      });

      rl.on('close', () => {
        console.log('\nExiting...');
        process.exit(0);
      });
    }
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
