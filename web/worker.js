// Runs one Otium program and posts its output back to the page. Each run gets
// a fresh worker, so the page stops a program by terminating it.
import createOtium from './otium-web.js';

onmessage = async ({ data: { source } }) => {
  const send = (stream) => (text) => postMessage({ stream, text: text + '\n' });
  try {
    await createOtium({
      arguments: ['--no-project', '/main.ot'],
      print: send('stdout'),
      printErr: send('stderr'),
      preRun: [(module) => module.FS.writeFile('/main.ot', source)],
      onExit: (status) => postMessage({ exit: status }),
    });
  } catch (error) {
    // Emscripten reports exit() by throwing once onExit has run.
    if (error?.name !== 'ExitStatus') postMessage({ stream: 'stderr', text: String(error) + '\n', exit: 1 });
  }
};
