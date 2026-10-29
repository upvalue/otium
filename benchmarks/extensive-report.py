#!/usr/bin/env python3
"""Run both benchmark suites against other languages and write an HTML report.

This is the occasional, slow comparison (expect it to take a long while),
meant for roughly once or twice a week. It is not the way to check whether a
change made Otium faster or slower: use `just bench`,
`benchmarks/r7rs/run.py --only NAME` or `just bench-c` for that.

It checks which reference implementations are installed, runs the R7RS ports
(with the upstream Guile programs when Guile is installed) and the
Programming-Language-Benchmarks problems at their full sizes, all pinned to
one CPU, and writes a self-contained HTML file. Missing implementations show
up in the report as "not installed" rather than stopping the run.

Pass --r7rs and --plb with existing run directories to report on earlier runs
instead of running. Passing several directories for a suite, for example from
two Otium commits, puts each build in its own column.
"""

import argparse
import csv
import datetime
import html
import json
import math
from pathlib import Path
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'benchmarks')]
from otium_tools import BUILD, run_directory
import common

# Reference implementations in column order, with how to ask their version.
REFERENCES = {
    'python': ['python3', '--version'],
    'sbcl': ['sbcl', '--version'],
    'lua': ['lua', '-v'],
    'luajit': ['luajit', '-v'],
    'guile': ['guile', '--version'],
}
TOOLS = {'taskset': 'pins every process to one CPU', 'cc': 'builds the peak-memory helper'}
SUITES = {
    'r7rs': 'R7RS benchmarks',
    'plb': 'Programming-Language-Benchmarks',
}
FAILED = ('incorrect', 'crashed', 'timeout')


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--r7rs', nargs='+', type=Path, metavar='DIR',
                        help='report on these R7RS run directories instead of running')
    parser.add_argument('--plb', nargs='+', type=Path, metavar='DIR',
                        help='report on these PLB run directories instead of running')
    parser.add_argument('--only', choices=list(SUITES), nargs='+', default=list(SUITES),
                        help='suites to include (default: both)')
    parser.add_argument('--runs', type=int, default=3, help='samples per benchmark (default: 3)')
    parser.add_argument('--timeout', type=float, default=600.0, help='seconds per sample')
    parser.add_argument('--size', choices=['small', 'full'], default='full',
                        help='PLB input size (default: full)')
    parser.add_argument('--otium-modes', nargs='+', choices=['native', 'vm'], default=['native', 'vm'],
                        help='Otium modes to run, each its own column (default: native vm)')
    parser.add_argument('--cpu', default='auto', help="CPU to pin to, or 'none' (default: auto)")
    parser.add_argument('--baseline',
                        help='implementation to compare against (default: the first native Otium column)')
    parser.add_argument('--build-label', default='unspecified')
    parser.add_argument('-o', '--output', type=Path, help='HTML file (default: under benchmarks/runs/report/)')
    return parser.parse_args()


def check_installed():
    """Print and return what is installed: {name: version or None}."""
    found = {name: common.version(command) for name, command in REFERENCES.items()}
    otium = BUILD / 'otium'
    found['otium'] = 'built' if otium.is_file() else None
    print('Implementations:')
    for name, version in found.items():
        print(f'  {name:8} {version or "not installed"}')
    print('Tools:')
    for name, purpose in TOOLS.items():
        print(f'  {name:8} {"found" if shutil.which(name) else "missing"} ({purpose})')
    return found


def run_suites(args, found):
    """Run the suites that weren't given as directories; return {suite: [dirs]}."""
    directories = {'r7rs': args.r7rs or [], 'plb': args.plb or []}
    if not found['otium']:
        sys.exit(f'error: {BUILD / "otium"} is missing; run just build')
    for suite in args.only:
        if directories[suite]:
            continue
        output = run_directory(suite)
        if suite == 'r7rs':
            command = [sys.executable, str(ROOT / 'benchmarks/r7rs/run.py'), '--runs', str(args.runs),
                       '--otium-modes', *args.otium_modes,
                       '--timeout', str(args.timeout), '--cpu', args.cpu, '--output', str(output),
                       '--build-label', args.build_label]
            if found['guile']:
                command += ['--reference', 'guile']
        else:
            command = [sys.executable, str(ROOT / 'benchmarks/plb/run.py'), '--runs', str(args.runs),
                       '--otium-modes', *args.otium_modes,
                       '--timeout', str(args.timeout), '--cpu', args.cpu, '--size', args.size,
                       '--output', str(output), '--build-label', args.build_label]
        print('$', ' '.join(command), flush=True)
        # The runners exit nonzero when anything fails; the report shows those cells.
        subprocess.run(command, cwd=ROOT)
        directories[suite] = [output]
    return directories


def load(directories):
    """Rows and metadata from run directories."""
    rows, metas = [], []
    for directory in directories:
        meta = json.loads((directory / 'metadata.json').read_text())
        meta['directory'] = str(directory)
        metas.append(meta)
        with (directory / 'results.csv').open() as source:
            for row in csv.DictReader(source):
                if row['implementation'] == 'guile-unavailable':
                    row['implementation'], row['status'] = 'guile', 'missing'
                rows.append(row)
    return rows, metas


def language(implementation):
    return implementation.split('-', 1)[0]


def seconds(row):
    value = row.get('reported_seconds') or row.get('wall_seconds')
    return float(value) if value else None


def summarize(rows):
    """{(benchmark, implementation): cell}, with benchmark and column orders."""
    groups, benchmarks, implementations = {}, [], []
    for row in rows:
        # R7RS rows name the input in the benchmark ("fib:40:5"); PLB rows have an input column.
        benchmark = row['benchmark'] + (f" {row['input']}" if row.get('size') else '')
        if benchmark not in benchmarks:
            benchmarks.append(benchmark)
        if row['implementation'] not in implementations:
            implementations.append(row['implementation'])
        groups.setdefault((benchmark, row['implementation']), []).append(row)
    cells = {}
    for key, group in groups.items():
        failed = [row for row in group if row['status'] != 'ok']
        first = group[0]
        cell = {'parity': first.get('parity', ''), 'threaded': first.get('threaded') == 'yes',
                'variant': first.get('variant', ''), 'source': first.get('source', '')}
        if failed:
            cell.update(status=failed[0]['status'], reason=failed[0].get('reason', ''))
        else:
            times = [seconds(row) for row in group]
            memory = [int(row['max_rss_kb']) for row in group if row.get('max_rss_kb')]
            cell.update(status='ok', time=statistics.median(times), low=min(times), high=max(times),
                        samples=len(times), rss=max(memory) if memory else None)
        cells[key] = cell

    def order(name):
        position = list(REFERENCES).index(language(name)) + 1 if language(name) in REFERENCES else 0
        return (position, implementations.index(name))
    return cells, benchmarks, sorted(implementations, key=order)


def geometric_mean(values):
    return math.exp(sum(math.log(v) for v in values) / len(values)) if values else None


def format_seconds(value):
    if value >= 100:
        return f'{value:.0f} s'
    if value >= 1:
        return f'{value:.2f} s'
    return f'{value * 1000:.0f} ms' if value >= 0.01 else f'{value * 1000:.1f} ms'


def format_ratio(ratio):
    return f'{ratio:.2f}×' if ratio < 10 else f'{ratio:.0f}×'


def shade(ratio):
    """Diverging class for ratio = implementation / baseline (log2 steps)."""
    step = max(-3, min(3, round(math.log2(ratio)))) if ratio > 0 else 0
    return f's{step:+d}'.replace('+0', '0')


def escape(text):
    return html.escape(str(text), quote=True)


STATUS_TEXT = {'incorrect': 'incorrect', 'crashed': 'crashed', 'timeout': 'timeout',
               'unsupported': '—', 'missing': 'not installed'}


def matrix(cells, benchmarks, implementations, baseline, notes, metric):
    """An HTML table of time or memory with ratios to the baseline column."""
    value_key = 'time' if metric == 'time' else 'rss'
    head = ''.join(f'<th scope="col">{escape(name)}</th>' for name in implementations)
    lines = [f'<table class="matrix"><thead><tr><th scope="col">benchmark</th>{head}</tr></thead><tbody>']
    ratios = {name: [] for name in implementations}
    for benchmark in benchmarks:
        base = cells.get((benchmark, baseline), {})
        base_value = base.get(value_key)
        row = [f'<th scope="row">{escape(benchmark)}</th>']
        for name in implementations:
            cell = cells.get((benchmark, name))
            if cell is None:
                row.append('<td class="empty"></td>')
                continue
            marks = ''.join(f'<sup><a href="#{escape(note)}">{escape(mark)}</a></sup>'
                            for mark, note in cell_notes(benchmark, name, cell, notes))
            value = cell.get(value_key)
            if cell['status'] != 'ok' or value is None:
                text = STATUS_TEXT.get(cell['status'], cell['status'])
                failed = ' failed' if cell['status'] in FAILED else ''
                title = escape(cell.get('reason', '')) if cell['status'] in FAILED else ''
                row.append(f'<td class="status{failed}" title="{title}">{escape(text)}{marks}</td>')
                continue
            shown = format_seconds(value) if metric == 'time' else f'{value / 1024:.1f} MB'
            if name == baseline or not base_value:
                row.append(f'<td class="base">{shown}{marks}</td>')
                continue
            ratio = value / base_value
            ratios[name].append(ratio)
            spread = (f'; samples {cell["samples"]}, {format_seconds(cell["low"])}–{format_seconds(cell["high"])}'
                      if metric == 'time' else '')
            title = f'{name}: {shown}, {format_ratio(ratio)} {escape(baseline)}{spread}'
            row.append(f'<td class="{shade(ratio)}" title="{escape(title)}">{shown}'
                       f'<span class="ratio">{format_ratio(ratio)}</span>{marks}</td>')
        lines.append('<tr>' + ''.join(row) + '</tr>')
    footer = ['<th scope="row">geometric mean</th>']
    for name in implementations:
        mean = geometric_mean(ratios[name])
        if name == baseline:
            footer.append('<td class="base">1.00×</td>')
        elif mean is None:
            footer.append('<td class="empty"></td>')
        else:
            footer.append(f'<td class="{shade(mean)}">{format_ratio(mean)}'
                          f'<span class="ratio">over {len(ratios[name])}</span></td>')
    lines.append('</tbody><tfoot><tr>' + ''.join(footer) + '</tr></tfoot></table>')
    return '\n'.join(lines), ratios


def cell_notes(benchmark, name, cell, notes):
    """Footnote marks for a cell, registering each footnote once."""
    marks = []
    # A and B ports are direct comparisons; C and D change what is measured.
    if cell.get('variant') == 'port' and cell.get('parity') in ('C', 'D'):
        key = f'note-port-{benchmark.split()[0]}'
        marks.append((cell['parity'], key))
    if cell.get('threaded'):
        marks.append(('‡', 'note-threaded'))
    for _, key in marks:
        notes.setdefault(key, None)
    return marks


def dot_chart(cells, benchmarks, implementations, baseline, metric):
    """Inline SVG: each benchmark's ratio to the baseline on a log scale, one dot per implementation."""
    value_key = 'time' if metric == 'time' else 'rss'
    others = [name for name in implementations if name != baseline]
    points = []
    for index, benchmark in enumerate(benchmarks):
        base = cells.get((benchmark, baseline), {}).get(value_key)
        for series, name in enumerate(others):
            value = cells.get((benchmark, name), {}).get(value_key)
            if base and value and cells[(benchmark, name)]['status'] == 'ok':
                points.append((index, series, name, value / base))
    if not points:
        return ''
    low = min(0.5, min(ratio for *_, ratio in points))
    high = max(2.0, max(ratio for *_, ratio in points))
    left, right, row_height, top = 170, 24, 22, 28
    width = 760
    height = top + row_height * len(benchmarks) + 30
    span = math.log(high) - math.log(low)

    def x(ratio):
        return left + (math.log(ratio) - math.log(low)) / span * (width - left - right)
    parts = [f'<svg class="dots" viewBox="0 0 {width} {height}" role="img" '
             f'aria-label="Ratio to {escape(baseline)} per benchmark, log scale">']
    ticks = [2 ** k for k in range(math.floor(math.log2(low)), math.ceil(math.log2(high)) + 1)]
    for tick in ticks:
        if low <= tick <= high:
            label = f'{tick:g}×' if tick >= 1 else f'1/{1 / tick:g}×'
            parts.append(f'<line class="{"axis" if tick == 1 else "grid"}" x1="{x(tick):.1f}" x2="{x(tick):.1f}" '
                         f'y1="{top - 8}" y2="{height - 24}"/>'
                         f'<text class="tick" x="{x(tick):.1f}" y="{height - 8}">{label}</text>')
    for index, benchmark in enumerate(benchmarks):
        y = top + row_height * index + row_height / 2
        parts.append(f'<text class="label" x="{left - 10}" y="{y + 4:.1f}">{escape(benchmark)}</text>')
    for index, series, name, ratio in points:
        y = top + row_height * index + row_height / 2
        title = f'{benchmarks[index]}: {name} {format_ratio(ratio)} {baseline}'
        parts.append(f'<circle class="series-{series % 8 + 1}" cx="{x(ratio):.1f}" cy="{y:.1f}" r="5">'
                     f'<title>{escape(title)}</title></circle>')
    parts.append(f'<text class="caption" x="{x(1):.1f}" y="{top - 14}">'
                 f'← faster than {escape(language(baseline))} · slower →</text>')
    parts.append('</svg>')
    legend = ''.join(f'<span class="key"><svg width="12" height="12"><circle class="series-{i % 8 + 1}" '
                     f'cx="6" cy="6" r="5"/></svg>{escape(name)}</span>' for i, name in enumerate(others))
    return f'<figure><div class="legend">{legend}</div>{"".join(parts)}</figure>'


def notes_html(notes, manifests):
    items = []
    for key in notes:
        if key == 'note-threaded':
            items.append(f'<li id="{key}">‡ Upstream program written for several threads or processes; '
                         'it ran on one CPU here.</li>')
            continue
        benchmark = key.removeprefix('note-port-')
        for suite, manifest in manifests.items():
            entry = manifest.get('benchmarks', {}).get(benchmark)
            if entry is None:
                continue
            port = entry.get('port', entry)
            items.append(f'<li id="{key}">{escape(port.get("parity", ""))} — {escape(benchmark)} ({suite}): '
                         f'{escape(port.get("deviation", ""))}</li>')
            break
    return f'<ol class="notes">{"".join(items)}</ol>' if items else ''


def suite_section(suite, rows, metas, baseline_option, manifests):
    cells, benchmarks, implementations = summarize(rows)
    otium = sorted((name for name in implementations if language(name) == 'otium'),
                   key=lambda name: not name.startswith('otium-native-'))
    baseline = baseline_option if baseline_option in implementations else (otium[0] if otium else implementations[0])
    notes = {}
    time_table, _ = matrix(cells, benchmarks, implementations, baseline, notes, 'time')
    has_memory = any(cell.get('rss') for cell in cells.values())
    memory_table = matrix(cells, benchmarks, implementations, baseline, notes, 'memory')[0] if has_memory else ''
    meta_lines = []
    for meta in metas:
        details = [f"commit {meta.get('commit', '')[:12]}{' (dirty)' if meta.get('dirty') else ''}",
                   f"label {meta.get('build_label')}", f"{meta.get('runs', '?')} samples",
                   f"CPU {meta.get('cpu') if meta.get('cpu') is not None else 'unpinned'}"]
        if meta.get('size'):
            details.append(f"size {meta['size']}")
        meta_lines.append(f'<li><code>{escape(meta["directory"])}</code>: {escape(", ".join(details))}</li>')
    timing = ('Times are the timed region each benchmark reports for itself; failed samples fail the cell.'
              if suite == 'r7rs' else
              'Times are whole-process wall time, including startup, as upstream measures them.')
    return f'''
<section>
  <h2>{escape(SUITES[suite])}</h2>
  <p>Median of each implementation's samples. The small figure under each time is its ratio to
  <strong>{escape(baseline)}</strong>: above 1× means it took longer. {timing}
  Hover a cell for its samples or failure reason.</p>
  <ul class="runs">{''.join(meta_lines)}</ul>
  <div class="scroll">{time_table}</div>
  {dot_chart(cells, benchmarks, implementations, baseline, 'time')}
  {f'<h3>Peak memory</h3><p>Largest peak RSS over the samples, measured from a helper process.</p><div class="scroll">{memory_table}</div>' if memory_table else ''}
  {notes_html(notes, manifests)}
</section>'''


STYLE = '''
:root { color-scheme: light; --bg: #fcfcfb; --surface: #ffffff; --ink: #0b0b0b; --ink-2: #52514e;
  --ink-3: #8a8984; --rule: #e4e3df; --fail: #d03b3b; --mid: #f0efec;
  --b3: #1c5cab; --b2: #6da7ec; --b1: #b7d3f6; --r1: #f6c4c0; --r2: #ec8a83; --r3: #b8302f;
  --on-strong: #ffffff;
  --series-1: #2a78d6; --series-2: #eb6834; --series-3: #1baf7a; --series-4: #eda100;
  --series-5: #e87ba4; --series-6: #008300; --series-7: #6250d6; --series-8: #e34948; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  color-scheme: dark; --bg: #121211; --surface: #1a1a19; --ink: #f0efec; --ink-2: #c3c2b7;
  --ink-3: #8f8e86; --rule: #34342f; --fail: #e66767; --mid: #383835;
  --b3: #6da7ec; --b2: #2f6ab3; --b1: #1f3f66; --r1: #5c2826; --r2: #9a3b37; --r3: #e66767;
  --on-strong: #121211;
  --series-1: #3987e5; --series-2: #d95926; --series-3: #199e70; --series-4: #c98500;
  --series-5: #d55181; --series-6: #008300; --series-7: #9085e9; --series-8: #e66767; } }
:root[data-theme="dark"] { color-scheme: dark; --bg: #121211; --surface: #1a1a19; --ink: #f0efec;
  --ink-2: #c3c2b7; --ink-3: #8f8e86; --rule: #34342f; --fail: #e66767; --mid: #383835;
  --b3: #6da7ec; --b2: #2f6ab3; --b1: #1f3f66; --r1: #5c2826; --r2: #9a3b37; --r3: #e66767;
  --on-strong: #121211;
  --series-1: #3987e5; --series-2: #d95926; --series-3: #199e70; --series-4: #c98500;
  --series-5: #d55181; --series-6: #008300; --series-7: #9085e9; --series-8: #e66767; }
body { margin: 0; background: var(--bg); color: var(--ink);
  font: 14px/1.5 ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 1100px; margin: 0 auto; padding: 24px 16px 64px; }
h1 { font-size: 24px; margin: 0 0 4px; } h2 { font-size: 19px; margin: 40px 0 8px; }
h3 { font-size: 15px; margin: 28px 0 8px; } p, li { color: var(--ink-2); max-width: 75ch; }
code { font: 12px ui-monospace, SFMono-Regular, Menlo, monospace; }
.scroll { overflow-x: auto; }
table { border-collapse: separate; border-spacing: 2px; font-variant-numeric: tabular-nums; }
th, td { padding: 4px 8px; text-align: right; white-space: nowrap; }
thead th { color: var(--ink-2); font-weight: 600; font-size: 12px; text-align: center; }
tbody th, tfoot th { text-align: left; font-weight: 500; }
td { background: var(--surface); border-radius: 4px; }
td .ratio { display: block; font-size: 11px; color: inherit; opacity: .8; }
td.base { box-shadow: inset 0 0 0 1px var(--rule); }
td.status { color: var(--ink-3); text-align: center; }
td.failed { color: var(--fail); font-weight: 600; }
td.empty { background: transparent; }
td.s0 { background: var(--mid); }
td.s-1 { background: var(--b1); } td.s-2 { background: var(--b2); }
td.s-3 { background: var(--b3); color: var(--on-strong); }
td.s1 { background: var(--r1); } td.s2 { background: var(--r2); }
td.s3 { background: var(--r3); color: var(--on-strong); }
tfoot td, tfoot th { font-weight: 600; }
sup a { color: var(--ink-2); text-decoration: none; margin-left: 2px; }
.scale { display: flex; gap: 2px; align-items: center; font-size: 12px; color: var(--ink-2); margin: 8px 0; flex-wrap: wrap; }
.scale span.swatch { width: 28px; height: 14px; border-radius: 3px; display: inline-block; }
figure { margin: 16px 0; } svg.dots { width: 100%; height: auto; max-width: 760px; }
svg .grid { stroke: var(--rule); } svg .axis { stroke: var(--ink-3); }
svg text { fill: var(--ink-2); font-size: 11px; } svg .tick, svg .caption { text-anchor: middle; }
svg .label { text-anchor: end; fill: var(--ink); }
svg circle { stroke: var(--surface); stroke-width: 2; }
''' + ''.join(f'svg .series-{i} {{ fill: var(--series-{i}); }}\n' for i in range(1, 9)) + '''
.legend { display: flex; gap: 16px; flex-wrap: wrap; font-size: 12px; color: var(--ink-2); }
.legend .key { display: inline-flex; align-items: center; gap: 6px; }
ul.runs, ul.installed { padding-left: 18px; font-size: 13px; }
.warning { color: var(--fail); }
ol.notes { font-size: 13px; padding-left: 18px; list-style: none; }
'''


def render(sections, found, metas, args):
    created = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d %H:%M UTC')
    hosts = sorted({meta.get('platform', '') for meta in metas})
    warnings = []
    if len(hosts) > 1:
        warnings.append('Runs come from different hosts: ' + ', '.join(hosts))
    if any(meta.get('dirty') for meta in metas):
        warnings.append('At least one run was made from a working tree with uncommitted changes.')
    if any(meta.get('cpu') is None for meta in metas):
        warnings.append('At least one run was not pinned to a CPU.')
    if any(meta.get('size') and meta['size'] != 'full' for meta in metas):
        warnings.append('PLB results use reduced input sizes.')
    installed = ''.join(f'<li>{escape(name)}: {escape(version or "not installed")}</li>'
                        for name, version in found.items())
    scale = ''.join(f'<span class="swatch" style="background: var(--{var})"></span>'
                    for var in ['b3', 'b2', 'b1', 'mid', 'r1', 'r2', 'r3'])
    return f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Otium Benchmarks</title>
<style>{STYLE}</style>
</head>
<body>
<main>
<h1>Otium benchmarks</h1>
<p>{escape(created)} on {escape(", ".join(hosts) or "unknown host")}.</p>
{''.join(f'<p class="warning">{escape(w)}</p>' for w in warnings)}
<div class="scale">faster than baseline {scale} slower than baseline (each step doubles)</div>
<details><summary>Implementations</summary><ul class="installed">{installed}</ul></details>
{''.join(sections)}
</main>
</body>
</html>
'''


def main():
    args = parse_args()
    found = check_installed()
    directories = run_suites(args, found) if not (args.r7rs and args.plb) else {'r7rs': args.r7rs, 'plb': args.plb}
    for suite in SUITES:
        if suite not in args.only:
            directories[suite] = []
    # Versions recorded by the runs are what the report should show.
    manifests = {'r7rs': json.loads((ROOT / 'benchmarks/r7rs/manifest.json').read_text()),
                 'plb': json.loads((ROOT / 'benchmarks/plb/manifest.json').read_text())}
    sections, all_metas = [], []
    for suite in SUITES:
        if not directories.get(suite):
            continue
        rows, metas = load(directories[suite])
        all_metas += metas
        for meta in metas:
            for name, version in (meta.get('versions') or {}).items():
                found[name] = version
        sections.append(suite_section(suite, rows, metas, args.baseline, manifests))
    output = args.output or run_directory('report').with_suffix('.html')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render(sections, found, all_metas, args))
    print(f'Report: {output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
