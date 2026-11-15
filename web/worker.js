// Owns one Otium runtime for the page. Each { source } message is a REPL
// entry; the reply says whether it ran, failed, exited or needs more lines.
// Output is posted as it is written, a line at a time, so a long entry shows
// progress. The page stops a runaway entry by terminating this worker.
import createOtium from './otium-web.js';

const STATUS = ['done', 'incomplete', 'failed', 'exited'];

function stream(name) {
  const decoder = new TextDecoder();
  let bytes = [];
  const flush = () => {
    if (!bytes.length) return;
    const text = decoder.decode(new Uint8Array(bytes), { stream: true });
    bytes = [];
    if (text) postMessage({ stream: name, text });
  };
  const write = (byte) => {
    if (byte === null) return;
    bytes.push(byte < 0 ? byte + 256 : byte);
    if (byte === 10) flush();
  };
  return { write, flush };
}

const stdout = stream('stdout'), stderr = stream('stderr');
const flush = () => { stdout.flush(); stderr.flush(); };

const otium = await createOtium({
  preRun: [(module) => module.FS.init(() => null, stdout.write, stderr.write)],
});
const started = otium.ccall('otium_start', 'boolean', [], []);
flush();
postMessage({ ready: started });

onmessage = ({ data: { source } }) => {
  let status;
  try {
    status = STATUS[otium.ccall('otium_eval', 'number', ['string'], [source])];
  } catch (error) {
    // A trap (stack overflow, out of memory) leaves the runtime unusable.
    stderr.flush();
    postMessage({ stream: 'stderr', text: `${error}\n` });
    status = 'crashed';
  }
  flush();
  postMessage({ status });
};
