// ✨ Evaluate in a dedicated worker so Stop can terminate even a tight loop.
import createOtium from './otium.mjs';
let output = '';
let truncated = false;
const limit = 65536;
const runtime = await createOtium({onOutput(text) {
  const room = limit - output.length;
  if (text.length > room) truncated = true;
  output += text.slice(0, room);
}});
if (!runtime._otium_web_init(0)) throw new Error('Could not initialize the C runtime');
self.postMessage({type: 'ready'});
self.onmessage = ({data}) => {
  if (data.type !== 'evaluate') return;
  output = '';
  truncated = false;
  try {
    const status = runtime.ccall('otium_web_eval', 'number', ['string'], [data.source]);
    self.postMessage({type: 'result', status, output: output + (truncated ? '\n[Output truncated]\n' : '')});
  } catch (error) {
    self.postMessage({type: 'fatal', message: String(error)});
  }
};
