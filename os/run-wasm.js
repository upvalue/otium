#!/usr/bin/env node

// run-wasm.js - Node.js test runner for Otium OS WASM build


const fs = require('fs');
const path = require('path');
const readline = require('readline');

// Load the compiled WASM module
const OtiumOS = require('./bin/kernel.js');

// Check if we're in test mode
const isTestMode = process.env.OTIUM_TEST_MODE === '1';

// Input buffer for getchar
const inputBuffer = [];

// Readline interface (only created in interactive mode)
let rl = null;

// Buffer to accumulate output
let outputBuffer = '';

// Module configuration
const Module = {
  exit: (status) => {
    if (isTestMode) {
      // In test mode, exit immediately with the status
      process.exit(status || 0);
    } else {
      // In interactive mode, allow user to exit gracefully
      process.exit(0);
    }
  },

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
