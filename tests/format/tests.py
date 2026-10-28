"""Formatter CLI selection, directory traversal, writes and project lookup."""
from harness import ROOT, test


@test('format cli')
def cli(t):
    directory = t.scratch / 'sources'
    directory.mkdir()
    nested = directory / 'nested'
    nested.mkdir()
    (directory / '.hidden').mkdir()
    (directory / '.hidden' / 'skip.ot').write_text('(bad')
    (directory / 'skip.txt').write_text('(bad')
    paths = [directory / 'one.ot', nested / 'two.ot']
    for path in paths:
        path.write_text('(def  x  1)\n')
    (t.scratch / 'project.ot').write_text('(format (like when wide))\n')
    project_case = nested / 'project.ot-example.ot'
    project_case.write_text('(wide a (b) (c))\n')
    command = [ROOT / 'otium', 'fmt']
    result = t.expect(t.run([*command, '--check', 'sources'], cwd=t.scratch), status=1)
    for path in paths:
        t.check_in(str(path.relative_to(t.scratch)), result.stdout)
    t.expect(t.run([*command, 'sources'], cwd=t.scratch), stdout='')
    for path in paths:
        t.check(path.read_text() == '(def x 1)\n', path.read_text())
    t.check(project_case.read_text() == '(wide a\n  (b)\n  (c))\n', project_case.read_text())
    t.expect(t.run([*command, '--check', 'sources'], cwd=t.scratch), stdout='')
    project_case.write_text('(bad')
    t.expect(t.run([*command, '--check', project_case]), status=1, stderr=[str(project_case)])
