#!/usr/bin/env node

// run-wasm.js - Node.js test runner for Otium OS WASM build


const fs = require('fs');
const path = require('path');
const readline = require('readline');

// Load the compiled WASM module
const OtiumOS = require('./otk/kernel.js');

// Input buffer for getchar
const inputBuffer = [];

// Create readline interface for user input
const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout,
  terminal: true
});

// Buffer to accumulate output
let outputBuffer = '';

// Module configuration
const Module = {
  exit: () => {
    process.exit(0);
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
    console.log('Otium OS WASM runtime initialized');
    console.log('Type commands and press Enter. Press Ctrl+C to exit.');
    console.log('');

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
