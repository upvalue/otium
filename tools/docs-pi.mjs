// Pi extension for tools/generate-docs.
import { execFile } from 'node:child_process';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const LIMIT = 8000;

export default function docsPi(pi) {
  // Pin normal service for OpenAI without changing other providers' payloads.
  pi.on('before_provider_request', (event, ctx) => {
    if (['openai-responses', 'openai-codex-responses'].includes(ctx.model?.api)) {
      return { ...event.payload, service_tier: 'default' };
    }
  });

  // Let the writer check its examples. Each run gets a fresh temporary
  // directory as its working directory, so files the code creates stay out
  // of the repository.
  pi.registerTool({
    name: 'run_otium',
    label: 'Run Otium',
    description: 'Run an Otium program with ./otium and return its output and exit status.',
    promptSnippet: 'Run Otium code to check that an example works',
    promptGuidelines: [
      'Use run_otium to check every code example before putting it in the document. ' +
        'Print results with write or println and compare them with the comments you show.',
    ],
    parameters: {
      type: 'object',
      properties: { code: { type: 'string', description: 'Otium source to run as a program' } },
      required: ['code'],
    },
    async execute(_id, params, signal) {
      const dir = await mkdtemp(path.join(tmpdir(), 'otium-docs-'));
      try {
        const file = path.join(dir, 'example.ot');
        await writeFile(file, params.code);
        const { code, output } = await new Promise((resolve) => {
          execFile(path.join(root, 'otium'), [file], { cwd: dir, timeout: 20000, signal },
            (error, stdout, stderr) => resolve({
              code: error ? (error.killed ? 'timeout' : error.code ?? 1) : 0,
              output: stdout + stderr,
            }));
        });
        const text = output.length > LIMIT ? output.slice(0, LIMIT) + '\n[output truncated]' : output;
        return { content: [{ type: 'text', text: `exit: ${code}\n${text}` }], details: {} };
      } finally {
        await rm(dir, { recursive: true, force: true });
      }
    },
  });
}
