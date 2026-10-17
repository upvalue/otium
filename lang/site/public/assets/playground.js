// ✨ Browser session controls. A terminated worker always starts a fresh runtime.
const editor = document.querySelector('#source');
const output = document.querySelector('#output');
const status = document.querySelector('#status');
const run = document.querySelector('#run');
const stop = document.querySelector('#stop');
const reset = document.querySelector('#reset');
const select = document.querySelector('#example');
let worker;
let timer;
let busy = false;
let loadTimer;

function show(text, error = false) {
  output.textContent = text;
  output.classList.toggle('error', error);
}

function start(message = 'Session ready.') {
  clearTimeout(timer);
  clearTimeout(loadTimer);
  worker?.terminate();
  run.disabled = true;
  stop.disabled = true;
  busy = false;
  status.textContent = 'Loading runtime…';
  worker = new Worker(new URL('./worker.js', import.meta.url), {type: 'module'});
  const current = worker;
  loadTimer = setTimeout(() => fail('Runtime loading timed out. Check your connection and reset.'), 30000);
  worker.onmessage = ({data}) => {
    if (current !== worker) return;
    if (data.type === 'ready') {
      clearTimeout(loadTimer);
      run.disabled = false;
      status.textContent = 'C runtime · WebAssembly';
      show(message);
    } else if (data.type === 'result') {
      clearTimeout(timer);
      busy = false;
      stop.disabled = true;
      run.disabled = data.status === 2;
      status.textContent = data.status === 1 ? 'Finished · session preserved' : data.status === 2 ? 'Session exited' : 'Condition raised · session preserved';
      show(data.output, data.status === 0);
    } else if (data.type === 'fatal') {
      fail('Runtime stopped: ' + data.message + '\nReset to start a fresh session.');
    }
  };
  worker.onerror = event => {
    if (current === worker) fail('Could not run the WASM runtime. ' + event.message + '\nReset to retry.');
  };
}

function fail(message) {
  clearTimeout(timer);
  clearTimeout(loadTimer);
  worker?.terminate();
  busy = false;
  run.disabled = true;
  stop.disabled = true;
  status.textContent = 'Runtime unavailable';
  show(message, true);
}

function evaluate() {
  if (run.disabled || busy || !editor.value.trim()) return;
  busy = true;
  run.disabled = true;
  stop.disabled = false;
  status.textContent = 'Running…';
  show('Running…');
  timer = setTimeout(() => start('Stopped after five seconds. Session reset.'), 5000);
  worker.postMessage({type: 'evaluate', source: editor.value});
}

run.addEventListener('click', evaluate);
stop.addEventListener('click', () => start('Stopped. Session reset.'));
reset.addEventListener('click', () => start('Session reset.'));
editor.addEventListener('keydown', event => {
  if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
    event.preventDefault();
    evaluate();
  }
});
try {
  const response = await fetch(new URL('./examples.json', import.meta.url));
  if (!response.ok) throw new Error(`Examples: HTTP ${response.status}`);
  const examples = await response.json();
  examples.forEach((example, index) => select.add(new Option(example.name, index)));
  editor.value = examples[0].source;
  select.addEventListener('change', () => { editor.value = examples[select.value].source; });
  start();
} catch (error) {
  fail(String(error));
}
