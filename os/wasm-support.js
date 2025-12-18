// wasm-support.js - Runtime support for Otium OS WASM builds
//
// This module provides:
// - In-memory filesystem with fs-in/fs-out sync (Node.js)
// - Graphics support via SDL (optional)
// - Module callbacks for WASM runtime

const fs = require('fs');
const path = require('path');

// =============================================================================
// Filesystem Storage
// =============================================================================

// In-memory filesystem storage: lowercase path -> { type: 'file'|'dir', data: Uint8Array, displayName: string }
// Case-insensitive like FAT, but preserves case for display
const fsStorage = new Map();

// Initialize root directory
fsStorage.set('/', { type: 'dir', data: null, children: new Map(), displayName: '' });

/**
 * Normalize a path: remove trailing slashes, handle . and ..
 * Returns lowercase path for lookups (case-insensitive)
 */
function normalizePath(p) {
  if (!p || p === '') return '/';
  if (!p.startsWith('/')) p = '/' + p;
  while (p.length > 1 && p.endsWith('/')) {
    p = p.slice(0, -1);
  }
  return p.toLowerCase(); // Case-insensitive
}

/**
 * Get parent path (lowercase)
 */
function getParentPath(p) {
  const normalized = normalizePath(p);
  if (normalized === '/') return '/';
  const lastSlash = normalized.lastIndexOf('/');
  if (lastSlash === 0) return '/';
  return normalized.slice(0, lastSlash);
}

/**
 * Get basename (last component of path, lowercase)
 */
function getBasename(p) {
  const normalized = normalizePath(p);
  if (normalized === '/') return '';
  const lastSlash = normalized.lastIndexOf('/');
  return normalized.slice(lastSlash + 1);
}

/**
 * Get display name from original path (preserves case)
 */
function getDisplayName(p) {
  if (!p || p === '' || p === '/') return '';
  let path = p;
  if (!path.startsWith('/')) path = '/' + path;
  while (path.length > 1 && path.endsWith('/')) {
    path = path.slice(0, -1);
  }
  const lastSlash = path.lastIndexOf('/');
  return path.slice(lastSlash + 1);
}

/**
 * Ensure all parent directories exist
 */
function ensureParentDirs(p) {
  const normalized = normalizePath(p);
  const parts = normalized.split('/').filter(x => x);
  let current = '';
  for (let i = 0; i < parts.length - 1; i++) {
    const displayPart = getDisplayName('/' + parts[i]);
    current += '/' + parts[i];
    if (!fsStorage.has(current)) {
      fsStorage.set(current, { type: 'dir', data: null, children: new Map(), displayName: displayPart });
      const parent = getParentPath(current);
      const parentEntry = fsStorage.get(parent);
      if (parentEntry && parentEntry.type === 'dir') {
        const basename = getBasename(current);
        parentEntry.children.set(basename, displayPart);
      }
    }
  }
}

/**
 * Load files from a directory into the virtual filesystem
 */
function loadFilesFromDir(baseDir) {
  if (!fs.existsSync(baseDir)) {
    return 0;
  }

  function loadDir(diskPath, virtualPath) {
    const entries = fs.readdirSync(diskPath, { withFileTypes: true });
    for (const entry of entries) {
      const diskEntryPath = path.join(diskPath, entry.name);
      const virtualEntryPath = virtualPath + '/' + entry.name;
      const normalizedPath = normalizePath(virtualEntryPath);

      if (entry.isDirectory()) {
        fsStorage.set(normalizedPath, { type: 'dir', data: null, children: new Map(), displayName: entry.name });
        const parent = getParentPath(normalizedPath);
        const parentEntry = fsStorage.get(parent);
        if (parentEntry && parentEntry.type === 'dir') {
          const basename = getBasename(normalizedPath);
          parentEntry.children.set(basename, entry.name);
        }
        loadDir(diskEntryPath, virtualEntryPath);
      } else if (entry.isFile()) {
        const content = fs.readFileSync(diskEntryPath);
        fsStorage.set(normalizedPath, { type: 'file', data: new Uint8Array(content), displayName: entry.name });
        const parent = getParentPath(normalizedPath);
        const parentEntry = fsStorage.get(parent);
        if (parentEntry && parentEntry.type === 'dir') {
          const basename = getBasename(normalizedPath);
          parentEntry.children.set(basename, entry.name);
        }
      }
    }
  }

  loadDir(baseDir, '');
  return fsStorage.size - 1;
}

/**
 * Save virtual filesystem to a directory
 */
function saveFilesToDir(outDir) {
  if (!fs.existsSync(outDir)) {
    fs.mkdirSync(outDir, { recursive: true });
  }

  // Build paths using display names for proper case
  function savePath(virtualPath, entry) {
    if (virtualPath === '/') return;

    // Reconstruct path using display names
    const parts = virtualPath.split('/').filter(x => x);
    let diskPath = outDir;
    let currentVirtualPath = '';

    for (const part of parts) {
      currentVirtualPath += '/' + part;
      const currentEntry = fsStorage.get(currentVirtualPath);
      const displayName = currentEntry ? currentEntry.displayName : part;
      diskPath = path.join(diskPath, displayName);
    }

    const diskParent = path.dirname(diskPath);
    if (!fs.existsSync(diskParent)) {
      fs.mkdirSync(diskParent, { recursive: true });
    }

    if (entry.type === 'dir') {
      if (!fs.existsSync(diskPath)) {
        fs.mkdirSync(diskPath, { recursive: true });
      }
    } else if (entry.type === 'file') {
      fs.writeFileSync(diskPath, entry.data);
    }
  }

  for (const [virtualPath, entry] of fsStorage) {
    savePath(virtualPath, entry);
  }
  return fsStorage.size - 1;
}

// =============================================================================
// Graphics Support
// =============================================================================

let sdl = null;
let sdlAvailable = false;
let window = null;
let pixelBuffer = null;

// SDL key (string) to DOM code mapping
// SDL returns key as a string like "a", "escape", etc.
const SDL_TO_DOM_KEYCODE = {
  // Special keys
  'escape': 'Escape',
  'return': 'Enter',
  'backspace': 'Backspace',
  'tab': 'Tab',
  'space': 'Space',
  // Letters (SDL gives lowercase)
  'a': 'KeyA', 'b': 'KeyB', 'c': 'KeyC', 'd': 'KeyD', 'e': 'KeyE',
  'f': 'KeyF', 'g': 'KeyG', 'h': 'KeyH', 'i': 'KeyI', 'j': 'KeyJ',
  'k': 'KeyK', 'l': 'KeyL', 'm': 'KeyM', 'n': 'KeyN', 'o': 'KeyO',
  'p': 'KeyP', 'q': 'KeyQ', 'r': 'KeyR', 's': 'KeyS', 't': 'KeyT',
  'u': 'KeyU', 'v': 'KeyV', 'w': 'KeyW', 'x': 'KeyX', 'y': 'KeyY',
  'z': 'KeyZ',
  // Numbers
  '0': 'Digit0', '1': 'Digit1', '2': 'Digit2', '3': 'Digit3', '4': 'Digit4',
  '5': 'Digit5', '6': 'Digit6', '7': 'Digit7', '8': 'Digit8', '9': 'Digit9',
  // Punctuation
  '-': 'Minus', '=': 'Equal', '[': 'BracketLeft', ']': 'BracketRight',
  ';': 'Semicolon', "'": 'Quote', '`': 'Backquote', '\\': 'Backslash',
  ',': 'Comma', '.': 'Period', '/': 'Slash',
  // Modifiers (SDL can report generic or specific names)
  'shift': 'ShiftLeft',
  'left shift': 'ShiftLeft', 'right shift': 'ShiftRight',
  'ctrl': 'ControlLeft',
  'left ctrl': 'ControlLeft', 'right ctrl': 'ControlRight',
  'alt': 'AltLeft',
  'left alt': 'AltLeft', 'right alt': 'AltRight',
};

// SDL scancode to DOM code mapping (fallback for when key is null/unknown)
const SDL_SCANCODE_TO_DOM = {
  229: 'ShiftLeft',   // Left Shift
  225: 'ShiftRight',  // Right Shift
  224: 'ControlLeft', // Left Ctrl
  228: 'ControlRight',// Right Ctrl
  226: 'AltLeft',     // Left Alt
  230: 'AltRight',    // Right Alt
};

function checkSdlAvailable() {
  try {
    require.resolve('@kmamal/sdl');
    sdlAvailable = true;
    return true;
  } catch (e) {
    sdlAvailable = false;
    return false;
  }
}

function graphicsInit(width, height) {
  if (!sdlAvailable) {
    return true; // Headless mode
  }

  try {
    if (!sdl) {
      sdl = require('@kmamal/sdl');
    }

    window = sdl.video.createWindow({
      title: 'Otium OS',
      width: width,
      height: height,
      resizable: false,
    });

    pixelBuffer = Buffer.allocUnsafe(width * height * 4);

    // Set up keyboard event handlers now that window exists
    window.on('keyDown', (event) => {
      let domCode = SDL_TO_DOM_KEYCODE[event.key];

      // Fallback to scancode if key is null or unmapped
      if (!domCode && event.scancode) {
        domCode = SDL_SCANCODE_TO_DOM[event.scancode];
      }

      if (domCode) {
        keyboardHandleEvent(domCode, true);
      } else {
        console.log(`[SDL] Unknown key: "${event.key}", scancode: ${event.scancode}`);
      }
    });

    window.on('keyUp', (event) => {
      let domCode = SDL_TO_DOM_KEYCODE[event.key];

      // Fallback to scancode if key is null or unmapped
      if (!domCode && event.scancode) {
        domCode = SDL_SCANCODE_TO_DOM[event.scancode];
      }

      if (domCode) {
        keyboardHandleEvent(domCode, false);
      }
    });

    return true;
  } catch (e) {
    console.error('Failed to initialize graphics:', e);
    sdlAvailable = false;
    return true; // Continue in headless mode
  }
}

function graphicsFlush(pixels, width, height) {
  if (!sdl || !window || !pixelBuffer) {
    return;
  }

  try {
    // Convert BGRA to RGBA
    for (let i = 0; i < width * height; i++) {
      const pixel = pixels[i];
      const offset = i * 4;
      pixelBuffer[offset + 0] = (pixel >> 16) & 0xFF; // R
      pixelBuffer[offset + 1] = (pixel >> 8) & 0xFF;  // G
      pixelBuffer[offset + 2] = pixel & 0xFF;         // B
      pixelBuffer[offset + 3] = (pixel >> 24) & 0xFF; // A
    }
    window.render(width, height, width * 4, 'rgba32', pixelBuffer);
  } catch (e) {
    if (e.message && e.message.includes('window is destroyed')) {
      console.log('\nWindow closed by user, exiting...');
      process.exit(0);
    }
    console.error('Graphics flush error:', e);
  }
}

function graphicsCleanup() {
  if (window) {
    window.destroy();
    window = null;
  }
  pixelBuffer = null;
}

// =============================================================================
// Keyboard Support
// =============================================================================

// Event queue: {code: uint16, flags: uint8}
const keyboardEventQueue = [];

// Modifier state
const keyboardModifiers = {
  shift: false,
  ctrl: false,
  alt: false,
};

// DOM key code to Linux input code mapping
const DOM_TO_LINUX_KEYCODE = {
  'Escape': 1,
  'Digit1': 2, 'Digit2': 3, 'Digit3': 4, 'Digit4': 5, 'Digit5': 6,
  'Digit6': 7, 'Digit7': 8, 'Digit8': 9, 'Digit9': 10, 'Digit0': 11,
  'Minus': 12, 'Equal': 13, 'Backspace': 14, 'Tab': 15,
  'KeyQ': 16, 'KeyW': 17, 'KeyE': 18, 'KeyR': 19, 'KeyT': 20,
  'KeyY': 21, 'KeyU': 22, 'KeyI': 23, 'KeyO': 24, 'KeyP': 25,
  'BracketLeft': 26, 'BracketRight': 27, 'Enter': 28,
  'KeyA': 30, 'KeyS': 31, 'KeyD': 32, 'KeyF': 33, 'KeyG': 34,
  'KeyH': 35, 'KeyJ': 36, 'KeyK': 37, 'KeyL': 38,
  'Semicolon': 39, 'Quote': 40, 'Backquote': 41, 'Backslash': 43,
  'KeyZ': 44, 'KeyX': 45, 'KeyC': 46, 'KeyV': 47, 'KeyB': 48,
  'KeyN': 49, 'KeyM': 50, 'Comma': 51, 'Period': 52, 'Slash': 53,
  'Space': 57,
  'ShiftLeft': 42, 'ShiftRight': 54,
  'ControlLeft': 29, 'ControlRight': 97,
  'AltLeft': 56, 'AltRight': 100,
};

function isModifierKey(code) {
  return code === 42 || code === 54 ||  // Shift
         code === 29 || code === 97 ||  // Ctrl
         code === 56 || code === 100;   // Alt
}

function keyboardHandleEvent(domCode, isKeyDown) {
  const linuxCode = DOM_TO_LINUX_KEYCODE[domCode];
  if (linuxCode === undefined) {
    console.log(`[KB] Unknown DOM code: ${domCode}`);
    return;
  }

  // Update modifier state
  if (linuxCode === 42 || linuxCode === 54) {
    keyboardModifiers.shift = isKeyDown;
  } else if (linuxCode === 29 || linuxCode === 97) {
    keyboardModifiers.ctrl = isKeyDown;
  } else if (linuxCode === 56 || linuxCode === 100) {
    keyboardModifiers.alt = isKeyDown;
  }

  // Build flags
  let flags = 0;
  if (isKeyDown) flags |= 0x01; // KEY_FLAG_PRESSED
  if (keyboardModifiers.shift) flags |= 0x02;
  if (keyboardModifiers.ctrl) flags |= 0x04;
  if (keyboardModifiers.alt) flags |= 0x08;

  // Don't queue modifier-only events (matches VirtIO backend behavior)
  if (!isModifierKey(linuxCode)) {
    keyboardEventQueue.push({ code: linuxCode, flags: flags });
  }
}

function keyboardInit() {
  console.log('Keyboard init');

  // Check if we're in a browser environment
  if (typeof window !== 'undefined' && typeof document !== 'undefined') {
    // Browser: attach to canvas or document
    const target = document.querySelector('canvas') || document;

    target.addEventListener('keydown', (e) => {
      e.preventDefault();
      keyboardHandleEvent(e.code, true);
    });

    target.addEventListener('keyup', (e) => {
      e.preventDefault();
      keyboardHandleEvent(e.code, false);
    });

    // Make sure canvas can receive keyboard events
    if (target.tagName === 'CANVAS') {
      target.tabIndex = 1;
      target.focus();
    }

    console.log('Keyboard: Browser mode initialized');
  } else if (typeof process !== 'undefined') {
    // Node.js: SDL keyboard events are registered in graphicsInit when window is created
    console.log('Keyboard: Node.js mode (SDL events registered in graphicsInit)');
  }

  return true;
}

function keyboardPoll() {
  if (keyboardEventQueue.length > 0) {
    return keyboardEventQueue.shift();
  }
  return null;
}

function keyboardCleanup() {
  keyboardEventQueue.length = 0;
}

// =============================================================================
// Filesystem Callbacks (for WASM Module)
// =============================================================================

const filesystemCallbacks = {
  fsExists(pathStr) {
    const normalized = normalizePath(pathStr);
    const entry = fsStorage.get(normalized);
    if (!entry) return null;
    return entry.type;
  },

  fsFileSize(pathStr) {
    const normalized = normalizePath(pathStr);
    const entry = fsStorage.get(normalized);
    if (!entry || entry.type !== 'file') return -1;
    return entry.data ? entry.data.length : 0;
  },

  fsReadFile(pathStr) {
    const normalized = normalizePath(pathStr);
    const entry = fsStorage.get(normalized);
    if (!entry || entry.type !== 'file') return null;
    return entry.data || new Uint8Array(0);
  },

  fsWriteFile(pathStr, data) {
    const normalized = normalizePath(pathStr);
    const entry = fsStorage.get(normalized);

    if (entry && entry.type === 'dir') {
      return false;
    }

    const displayName = getDisplayName(pathStr);

    if (!entry) {
      ensureParentDirs(normalized);
    }

    fsStorage.set(normalized, { type: 'file', data: data, displayName: displayName });

    const parent = getParentPath(normalized);
    const parentEntry = fsStorage.get(parent);
    if (parentEntry && parentEntry.type === 'dir') {
      const basename = getBasename(normalized);
      parentEntry.children.set(basename, displayName);
    }

    return true;
  },

  fsCreateFile(pathStr) {
    const normalized = normalizePath(pathStr);

    if (fsStorage.has(normalized)) {
      return false;
    }

    const displayName = getDisplayName(pathStr);

    ensureParentDirs(normalized);
    fsStorage.set(normalized, { type: 'file', data: new Uint8Array(0), displayName: displayName });

    const parent = getParentPath(normalized);
    const parentEntry = fsStorage.get(parent);
    if (parentEntry && parentEntry.type === 'dir') {
      const basename = getBasename(normalized);
      parentEntry.children.set(basename, displayName);
    }

    return true;
  },

  fsCreateDir(pathStr) {
    const normalized = normalizePath(pathStr);

    if (fsStorage.has(normalized)) {
      return false;
    }

    const displayName = getDisplayName(pathStr);

    ensureParentDirs(normalized);
    fsStorage.set(normalized, { type: 'dir', data: null, children: new Map(), displayName: displayName });

    const parent = getParentPath(normalized);
    const parentEntry = fsStorage.get(parent);
    if (parentEntry && parentEntry.type === 'dir') {
      const basename = getBasename(normalized);
      parentEntry.children.set(basename, displayName);
    }

    return true;
  },

  fsDeleteFile(pathStr) {
    const normalized = normalizePath(pathStr);
    const entry = fsStorage.get(normalized);

    if (!entry || entry.type !== 'file') {
      return false;
    }

    const parent = getParentPath(normalized);
    const parentEntry = fsStorage.get(parent);
    if (parentEntry && parentEntry.type === 'dir') {
      parentEntry.children.delete(getBasename(normalized));
    }

    fsStorage.delete(normalized);
    return true;
  },

  fsDeleteDir(pathStr) {
    const normalized = normalizePath(pathStr);
    const entry = fsStorage.get(normalized);

    if (!entry || entry.type !== 'dir') {
      return false;
    }

    if (entry.children && entry.children.size > 0) {
      return false;
    }

    const parent = getParentPath(normalized);
    const parentEntry = fsStorage.get(parent);
    if (parentEntry && parentEntry.type === 'dir') {
      parentEntry.children.delete(getBasename(normalized));
    }

    fsStorage.delete(normalized);
    return true;
  },

  fsListDir(pathStr) {
    const normalized = normalizePath(pathStr);
    const entry = fsStorage.get(normalized);
    if (!entry || entry.type !== 'dir') {
      return null;
    }

    // Return array of entry names using display names (with '/' suffix for directories)
    const result = [];
    if (entry.children) {
      for (const [lowercaseBasename, displayName] of entry.children) {
        const childPath = normalized === '/' ? '/' + lowercaseBasename : normalized + '/' + lowercaseBasename;
        const childEntry = fsStorage.get(childPath);
        if (childEntry) {
          result.push(childEntry.type === 'dir' ? displayName + '/' : displayName);
        }
      }
    }
    return result;
  },
};

// =============================================================================
// Exports
// =============================================================================

module.exports = {
  // Filesystem
  loadFilesFromDir,
  saveFilesToDir,
  filesystemCallbacks,

  // Graphics
  checkSdlAvailable,
  graphicsInit,
  graphicsFlush,
  graphicsCleanup,

  // Keyboard
  keyboardInit,
  keyboardPoll,
  keyboardCleanup,
  keyboardHandleEvent,
};
