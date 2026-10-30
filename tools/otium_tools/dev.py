"""Client side of the development server protocol: framing, a reader for responses, and finding
running servers."""
import os
from pathlib import Path
import socket
import struct

LIMIT = 4 * 1024 * 1024
DIRECTORY = Path('/tmp/otium-%d' % os.geteuid())


class Keyword(str):
    def __repr__(self):
        return ':' + self


class Symbol(str):
    pass


class Opaque(str):
    """Printed objects the reader can't rebuild, such as #<fn>."""


def read(text):
    """Read one printed Otium value. Tables become dicts, lists and arrays become lists."""
    value, end = _read(text, _skip(text, 0))
    if _skip(text, end) != len(text):
        raise ValueError('trailing text after value')
    return value


ESCAPES = {'n': '\n', 't': '\t', 'r': '\r', '\\': '\\', '"': '"', '0': '\0', 'e': '\x1b'}
STRING_ESCAPES = {value: '\\' + key for key, value in ESCAPES.items()}
DELIMITERS = set(' \t\r\n()[]{}"')


def _skip(text, i):
    while i < len(text) and text[i] in ' \t\r\n':
        i += 1
    return i


def _read(text, i):
    if i >= len(text):
        raise ValueError('unexpected end of data')
    c = text[i]
    if c in ')]}':
        raise ValueError('unexpected closing delimiter ' + c)
    if c in '([{':
        close = {'(': ')', '[': ']', '{': '}'}[c]
        items = []
        i = _skip(text, i + 1)
        while i < len(text) and text[i] != close:
            item, i = _read(text, i)
            items.append(item)
            i = _skip(text, i)
        if i >= len(text):
            raise ValueError('unclosed ' + c)
        if c == '{':
            if len(items) % 2:
                raise ValueError('table requires key/value pairs')
            try:
                return dict(zip(items[::2], items[1::2])), i + 1
            except TypeError as error:
                raise ValueError('unsupported table key') from error
        return items, i + 1
    if c == '"':
        out = []
        i += 1
        while i < len(text) and text[i] != '"':
            if text[i] == '\\':
                i += 1
                if i >= len(text):
                    raise ValueError('unclosed string')
                if text[i] == 'x':
                    end = text.index(';', i)
                    out.append(chr(int(text[i + 1:end], 16)))
                    i = end + 1
                    continue
                if text[i] not in ESCAPES:
                    raise ValueError('invalid string escape')
                out.append(ESCAPES[text[i]])
            else:
                out.append(text[i])
            i += 1
        if i >= len(text):
            raise ValueError('unclosed string')
        return ''.join(out), i + 1
    if text.startswith('#<', i):
        end = text.index('>', i)
        return Opaque(text[i:end + 1]), end + 1
    start = i
    while i < len(text) and text[i] not in DELIMITERS:
        i += 1
    token = text[start:i]
    if token == '#t':
        return True, i
    if token == '#f':
        return False, i
    if token == 'nil':
        return None, i
    if token.startswith(':') and len(token) > 1:
        return Keyword(token[1:]), i
    for kind in (int, float):
        try:
            return kind(token), i
        except ValueError:
            pass
    return Symbol(token), i


def encode(value):
    """Print a Python value as Otium data for a request."""
    if isinstance(value, bool):
        return '#t' if value else '#f'
    if value is None:
        return 'nil'
    if isinstance(value, Keyword):
        return ':' + value
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, str):
        return '"' + ''.join(STRING_ESCAPES.get(c, '\\x%x;' % ord(c) if ord(c) < 32 else c)
                             for c in value) + '"'
    if isinstance(value, dict):
        return '{' + ' '.join(encode(k) + ' ' + encode(v) for k, v in value.items()) + '}'
    raise TypeError('cannot encode %r' % (value,))


class Connection:
    def __init__(self, path, timeout=None):
        self.path = str(path)
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(timeout)
        try:
            self.socket.connect(self.path)
        except OSError:
            self.socket.close()
            raise
        self.buffer = bytearray()
        self.serial = 0

    def close(self):
        self.socket.close()

    def send(self, op, **fields):
        self.serial += 1
        request = {Keyword('id'): self.serial, Keyword('op'): Keyword(op)}
        request.update({Keyword(k.replace('_', '-')): v for k, v in fields.items()})
        self.send_text(encode(request))
        return self.serial

    def send_text(self, text):
        data = text.encode('utf-8')
        if len(data) > LIMIT:
            raise ValueError('request exceeds 4 MiB')
        self.socket.sendall(struct.pack('!I', len(data)) + data)

    def receive(self):
        """Read one frame as text, blocking."""
        while True:
            if len(self.buffer) >= 4:
                size, = struct.unpack('!I', self.buffer[:4])
                if size > LIMIT:
                    raise ValueError('response exceeds 4 MiB')
                if len(self.buffer) >= size + 4:
                    text = self.buffer[4:4 + size].decode('utf-8')
                    del self.buffer[:4 + size]
                    return text
            data = self.socket.recv(65536)
            if not data:
                raise EOFError('development server disconnected')
            self.buffer.extend(data)

    def frames(self):
        """Read whatever frames are available after one recv."""
        data = self.socket.recv(65536)
        if not data:
            raise EOFError('development server disconnected')
        self.buffer.extend(data)
        result = []
        while len(self.buffer) >= 4:
            size, = struct.unpack('!I', self.buffer[:4])
            if size > LIMIT:
                raise ValueError('response exceeds 4 MiB')
            if len(self.buffer) < size + 4:
                break
            result.append(self.buffer[4:4 + size].decode('utf-8'))
            del self.buffer[:4 + size]
        return result

    def request(self, op, on_event=None, **fields):
        """Send a request and return its response as data. Events seen while waiting go to
        on_event; returning True from it stops waiting and returns that event instead."""
        ident = self.send(op, **fields)
        while True:
            message = read(self.receive())
            if 'event' in message and 'id' not in message:
                if on_event and on_event(message):
                    return message
                continue
            if message.get('id') is False or message.get('id') == ident:
                return message


def status(path, timeout=1.0):
    """Ask one socket for its status. Returns None for a stale socket, which is removed, and
    raises OSError for one that exists but doesn't answer."""
    try:
        connection = Connection(path, timeout)
    except ConnectionRefusedError:
        try:
            os.unlink(path)
        except OSError:
            pass
        return None
    except FileNotFoundError:
        return None
    try:
        return connection.request('status')['value']
    finally:
        connection.close()


def servers(timeout=1.0):
    """List (path, status) for every socket in the shared directory. A server that doesn't
    answer in time is listed with status None."""
    if not DIRECTORY.is_dir():
        return []
    result = []
    for path in sorted(DIRECTORY.glob('*.sock')):
        try:
            info = status(path, timeout)
        except (socket.timeout, OSError, EOFError, ValueError):
            result.append((path, None))
            continue
        if info is not None:
            result.append((path, info))
    return result


def _contains(parent, child):
    try:
        Path(child).relative_to(parent)
        return True
    except ValueError:
        return False


def choose(found, cwd=None):
    """Pick a server: the only one, or the one whose cwd most closely contains this
    directory."""
    if len(found) == 1:
        return found[0]
    here = Path(cwd or os.getcwd()).resolve()
    matches = [(len(Path(entry[1]['cwd']).resolve().parts), entry) for entry in found
               if entry[1] and entry[1].get('cwd') and
               _contains(Path(entry[1]['cwd']).resolve(), here)]
    deepest = max((depth for depth, _ in matches), default=None)
    closest = [entry for depth, entry in matches if depth == deepest]
    return closest[0] if len(closest) == 1 else None
