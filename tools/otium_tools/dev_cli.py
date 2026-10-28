"""otium-dev: one request to a running Otium development server per call."""
import argparse
import os
from pathlib import Path
import socket
import sys
import time

from otium_tools import dev

OK, FAILED, NO_SERVER, TIMED_OUT, PAUSED = 0, 1, 2, 3, 4


class Exit(Exception):
    def __init__(self, status, message=''):
        super().__init__(message)
        self.status = status


def show(value):
    """Print data roughly as Otium's write would."""
    if isinstance(value, bool):
        return '#t' if value else '#f'
    if value is None:
        return 'nil'
    if isinstance(value, dev.Keyword):
        return ':' + value
    if isinstance(value, (dev.Symbol, dev.Opaque)):
        return str(value)
    if isinstance(value, str):
        return dev.encode(value)
    if isinstance(value, list):
        return '(' + ' '.join(map(show, value)) + ')'
    if isinstance(value, dict):
        return '{' + ' '.join(show(k) + ' ' + show(v) for k, v in value.items()) + '}'
    return str(value)


def show_condition(condition):
    lines = ['error: ' + ' '.join([str(condition.get('message', 'error'))] +
                                  [show(i) for i in condition.get('irritants') or []])]
    source = condition.get('source')
    if source:
        lines.append('  at %s:%s' % (source.get('path'), source.get('line')))
    return '\n'.join(lines)


def show_event(event):
    kind = event.get('event')
    fields = {k: v for k, v in event.items() if k not in ('event', 'seq', 'condition')}
    text = '#%s %s' % (event.get('seq', '-'), kind)
    if fields:
        text += ' ' + ' '.join('%s=%s' % (k, show(v)) for k, v in fields.items())
    if event.get('condition'):
        text += '\n' + '\n'.join('  ' + line for line in show_condition(event['condition']).split('\n'))
    return text


def pick(args):
    if args.socket:
        return Path(args.socket)
    if args.pid:
        return dev.DIRECTORY / ('%d.sock' % args.pid)
    found = dev.servers()
    if not found:
        raise Exit(NO_SERVER, 'no running development server; start one with ./otium --dev FILE')
    chosen = dev.choose(found)
    if chosen is None:
        raise Exit(NO_SERVER, 'several development servers are running; pass --pid\n' +
                   listing(found))
    return chosen[0]


def listing(found):
    rows = [('PID', 'STATE', 'CWD', 'PROGRAM')]
    for path, info in found:
        if info is None:
            rows.append((path.stem, 'busy', '?', '?'))
            continue
        program = ' '.join([info.get('script') or '-'] + list(info.get('arguments') or []))
        rows.append((str(info['pid']), info['state'], info['cwd'], program))
    widths = [max(len(row[i]) for row in rows) for i in range(3)]
    return '\n'.join('  '.join(cell.ljust(widths[i]) for i, cell in enumerate(row[:3])) +
                     '  ' + row[3] for row in rows)


def code_input(args):
    sources = [x for x in (args.code is not None, args.file is not None) if x]
    if len(sources) != 1:
        raise Exit(FAILED, 'pass CODE, --file PATH, or - to read stdin')
    if args.file is not None:
        path = Path(args.file).resolve()
        return path.read_text(encoding='utf-8'), str(path)
    if args.code == '-':
        return sys.stdin.read(), '<stdin>'
    return args.code, None


def paused(event):
    return event.get('event') == 'paused'


def report_pause(event):
    print('paused (%s after %sms in %s); the request is still running.' %
          (event.get('reason'), event.get('elapsed-ms'), event.get('ns')), file=sys.stderr)
    print('Inspect with otium-dev eval, then otium-dev continue or otium-dev abort.', file=sys.stderr)
    return PAUSED


def finish(args, response):
    """Print an eval or load response and return the exit status."""
    if args.raw:
        print(show(response))
        return OK if response.get('ok') else FAILED
    sys.stdout.write(response.get('output') or '')
    if response.get('ok'):
        if args.command == 'load':
            print('loaded %d forms' % len(response.get('values') or []))
        else:
            print(response.get('value'))
        if response.get('truncated'):
            print('(truncated to %d characters; pass --max-chars)' % args.max_chars, file=sys.stderr)
        return OK
    sys.stdout.flush()
    print(show_condition(response.get('condition') or {}), file=sys.stderr)
    return FAILED


def run(args):
    if args.command == 'ls':
        found = dev.servers()
        if args.raw:
            for path, info in found:
                print(str(path), show(info))
        elif found:
            print(listing(found))
        return OK

    path = pick(args)
    try:
        connection = dev.Connection(path, args.timeout or None)
    except FileNotFoundError:
        raise Exit(NO_SERVER, 'no development server at %s' % path)
    except ConnectionRefusedError:
        raise Exit(NO_SERVER, 'development server at %s is not running' % path)
    try:
        return command(args, connection)
    finally:
        connection.close()


def command(args, connection):
    c = args.command
    if c == 'status':
        response = connection.request('status')
        if args.raw:
            print(show(response))
        else:
            for key, value in response['value'].items():
                print('%s: %s' % (key, value if isinstance(value, str) else show(value)))
        return OK

    if c == 'eval':
        code, source = code_input(args)
        fields = {'code': code, 'ns': args.ns, 'max_chars': args.max_chars}
        if source:
            fields['path'] = source
        response = connection.request('eval', on_event=paused, **fields)
        return report_pause(response) if 'id' not in response else finish(args, response)

    if c == 'load':
        status = OK
        for name in args.paths:
            path = str(Path(name).resolve())
            response = connection.request('load', on_event=paused, path=path, max_chars=args.max_chars)
            if 'id' not in response:
                return report_pause(response)
            if not args.raw and len(args.paths) > 1:
                print(name + ': ', end='')
            status = finish(args, response)
            if status != OK:
                return status
        return status

    if c in ('interrupt', 'continue', 'abort'):
        response = connection.request(c)
        if args.raw:
            print(show(response))
        elif not response.get('ok'):
            print(show_condition(response.get('condition') or {}), file=sys.stderr)
            return FAILED
        else:
            print('paused' if c == 'interrupt' else 'resumed' if c == 'continue' else 'aborted')
        return OK

    if c == 'events':
        response = connection.request('events', since=args.since)
        for event in response.get('value') or []:
            print(show(event) if args.raw else show_event(event))
        return OK

    if c == 'capture':
        out = Path(args.path).resolve()
        if out.suffix.lower() not in ('.png', '.bmp'):
            raise Exit(FAILED, 'capture writes .png or .bmp')
        before = out.stat().st_mtime_ns if out.exists() else None
        code = ('(require otium.ui) (let ((ws (otium.ui/windows))) '
                '(when (<= (length ws) %d) (error "no window" %d)) '
                '(otium.ui/capture! (car (drop %d ws)) %s) nil)'
                % (args.window, args.window, args.window, dev.encode(str(out))))
        response = connection.request('eval', code=code, ns='user')
        if not response.get('ok'):
            print(show_condition(response.get('condition') or {}), file=sys.stderr)
            return FAILED
        deadline = time.monotonic() + (args.timeout or 10)
        while time.monotonic() < deadline:
            if out.exists() and out.stat().st_mtime_ns != before and out.stat().st_size > 0:
                print('saved ' + str(out))
                return OK
            time.sleep(0.05)
        raise Exit(TIMED_OUT, 'no frame was captured; check otium-dev events for frame errors')

    raise Exit(FAILED, 'unknown command ' + c)


def parser():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument('--pid', type=int, help='Server process id.')
    common.add_argument('--socket', help='Server socket path.')
    common.add_argument('--timeout', type=float, default=10,
                        help='Seconds to wait for a response; 0 waits forever (default: 10).')
    common.add_argument('--raw', action='store_true', help='Print responses as received data.')

    top = argparse.ArgumentParser(
        prog='otium-dev', description=__doc__,
        epilog='Exit status: 0 ok, 1 Otium error, 2 no server, 3 timed out, 4 program paused.')
    sub = top.add_subparsers(dest='command', required=True)
    sub.add_parser('ls', parents=[common], help='List running servers.')
    sub.add_parser('status', parents=[common], help='Show one server\'s status.')
    p = sub.add_parser('eval', parents=[common], help='Evaluate code.')
    p.add_argument('code', nargs='?', help='Code to evaluate, or - for stdin.')
    p.add_argument('--file', help='Evaluate the forms in this file.')
    p.add_argument('--ns', default='user', help='Namespace to evaluate in (default: user).')
    p.add_argument('--max-chars', type=int, default=10000,
                   help='Truncate values and output; 0 disables (default: 10000).')
    p = sub.add_parser('load', parents=[common], help='Reload files as their namespaces.')
    p.add_argument('paths', nargs='+')
    p.add_argument('--max-chars', type=int, default=10000)
    for name, text in (('interrupt', 'Pause running code.'), ('continue', 'Resume paused code.'),
                       ('abort', 'Abort paused code.')):
        sub.add_parser(name, parents=[common], help=text)
    p = sub.add_parser('events', parents=[common], help='Show recent events.')
    p.add_argument('--since', type=int, default=0, help='Only events after this number.')
    p = sub.add_parser('capture', parents=[common], help='Save the next UI frame as PNG or BMP.')
    p.add_argument('path')
    p.add_argument('--window', type=int, default=0, help='Window index (default: 0).')
    return top


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        return run(args)
    except Exit as e:
        if str(e):
            print('otium-dev: ' + str(e), file=sys.stderr)
        return e.status
    except socket.timeout:
        print('otium-dev: server busy; try otium-dev interrupt', file=sys.stderr)
        return TIMED_OUT
    except (OSError, EOFError, ValueError) as e:
        print('otium-dev: ' + str(e), file=sys.stderr)
        return NO_SERVER


if __name__ == '__main__':
    sys.exit(main())
