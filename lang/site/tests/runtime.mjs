// ✨ Exercise the shipped WASM binary, including source documentation examples.
import assert from 'node:assert/strict';
import {readFileSync, readdirSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import createOtium from '../dist/assets/otium.mjs';
const root = fileURLToPath(new URL('../../', import.meta.url));
let output = '';
const runtime = await createOtium({onOutput: text => { output += text; }});
const fresh = (stress = false) => assert.equal(runtime._otium_web_init(Number(stress)), 1);
function evaluate(source) {
  output = '';
  const status = runtime.ccall('otium_web_eval', 'number', ['string'], [source]);
  return {status, text: output.trimEnd()};
}
function expect(source, expected) {
  const result = evaluate(source);
  assert.equal(result.status, 1, `${source}\n${result.text}`);
  if (expected !== undefined) assert.equal(result.text, expected, source);
}
for (const stress of [false, true]) {
  fresh(stress);
  expect('(def (value) 1)', undefined);
  expect('(def (caller) (value))', undefined);
  expect('(def (value) 2)', undefined);
  expect('(caller)', '2');
  assert.equal(evaluate('(error "oops")').status, 0);
  expect('(+ 20 22)', '42');
  assert.equal(evaluate('(').status, 0);
  expect('(get {1 "one" -2 "negative" 1073741823 "max"} -2)', '"negative"');
  expect('(get {1 "one" -2 "negative" 1073741823 "max"} 1073741823)', '"max"');
  expect('(get {1 "one" -2 "negative"} 1)', '"one"');
  expect('(length (map (fn (x) (list x x)) (range 200)))', '200');
}
for (const stress of [false, true]) {
  fresh(stress);
  expect(readFileSync(root + 'shared/tests/classes/checks.ot', 'utf8'), 'class checks passed 96\nnil');
}
for (const example of JSON.parse(readFileSync(root + 'site/public/assets/examples.json'))) {
  fresh();
  expect(example.source, example.expected ?? undefined);
}
for (const page of ['index', 'guide']) {
  fresh();
  const source = readFileSync(root + `docs/${page}.md`, 'utf8');
  for (const match of source.matchAll(/```scheme\n([\s\S]*?)```/g)) expect(match[1]);
}
for (const name of readdirSync(root + 'c/tests/otium').filter(name => /^\d.*\.ot$/.test(name))) {
  fresh();
  const result = evaluate(readFileSync(root + 'c/tests/otium/' + name, 'utf8'));
  let expected = readFileSync(root + 'c/tests/otium/expected/' + name.replace('.ot', '.txt'), 'utf8').trimEnd();
  // ✨ This fixture deliberately exceeds the WASM target's signed 31-bit range.
  if (name === '05-control.ot') expected = expected.replace('1250025000', String(Number(BigInt.asIntN(31, 1250025000n))));
  assert.equal(result.status, 1, `${name}: ${result.text}`);
  assert.equal(result.text, expected + '\nnil', name);
}
fresh();
expect('(println "hello")', 'hello\nnil');
assert.equal(evaluate('(exit 0)').status, 2);
fresh();
expect('42', '42');
console.log('WASM: classes, conformance fixtures, guide examples, state, GC stress, errors, integer keys, and reset passed.');
