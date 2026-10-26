import argparse
import importlib.machinery
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
loader = importlib.machinery.SourceFileLoader('generate_docs', str(ROOT / 'tools/generate-docs'))
spec = importlib.util.spec_from_loader(loader.name, loader)
generator = importlib.util.module_from_spec(spec)
loader.exec_module(generator)


def events(text, reason='stop'):
    return json.dumps({'type': 'agent_end', 'messages': [
        {'role': 'assistant', 'stopReason': 'toolUse', 'content': [{'type': 'text', 'text': 'Reading files'}]},
        {'role': 'assistant', 'stopReason': reason, 'content': [
            {'type': 'thinking', 'thinking': 'private'}, {'type': 'text', 'text': text}]},
    ]})


class GenerationTests(unittest.TestCase):
    def test_final_reply_excludes_thinking_and_intermediate_messages(self):
        self.assertEqual(generator.final_reply(events('# ✨ Test')), '# ✨ Test')
        for output in ['', 'not json', events('# ✨ Test', 'length'), events('# ✨ Test', 'error')]:
            with self.assertRaises(ValueError):
                generator.final_reply(output)

    def test_command_keeps_tools_read_only_and_allows_provider_override(self):
        args = argparse.Namespace(provider='other', model='model', thinking='medium')
        command = generator.pi_command('rules', args)
        self.assertEqual(command[command.index('--tools') + 1], 'read,grep,find,ls')
        self.assertEqual(command[command.index('--provider') + 1], 'other')
        self.assertIn('--no-extensions', command)
        self.assertIn('--no-session', command)

    def test_failed_generation_preserves_existing_document(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as folder:
            target = Path(folder) / 'test.md'
            target.write_text('original\n')
            spec = target.with_suffix('.md.spec')
            spec.write_text('---\ntitle: Test\nprompt: Write it\n---\n')
            args = argparse.Namespace(provider='openai-codex', model='gpt-6-astra',
                                      thinking='medium', dry_run=False, timeout=30)
            for reply in ['bad json', events('# ✨ Wrong'), events('# ✨ Test', 'length')]:
                with patch.object(generator.subprocess, 'run', return_value=argparse.Namespace(
                        returncode=0, stdout=reply, stderr='')):
                    self.assertFalse(generator.generate(spec, '', args))
                    self.assertEqual(target.read_text(), 'original\n')
            with patch.object(generator.subprocess, 'run', return_value=argparse.Namespace(
                    returncode=0, stdout=events('# ✨ Test\n\nA document.'), stderr='')):
                self.assertTrue(generator.generate(spec, '', args))
                self.assertEqual(target.read_text(), '# ✨ Test\n\nA document.\n')


if __name__ == '__main__':
    unittest.main()
