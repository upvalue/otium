#!/usr/bin/env python3
"""Attach to Otium, or expose the development protocol as stdio MCP tools."""
import argparse
import json
import os
from pathlib import Path
import re
import selectors
import socket
import struct
import sys

from otium_tools import dev

LIMIT = 4 * 1024 * 1024


def default_socket():
    found = dev.servers()
    chosen = dev.choose(found)
    if chosen is None:
        raise ValueError('no running development server' if not found else
                         'several development servers are running; pass a socket path')
    return chosen[0]


class Connection:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.connect(str(path))
        self.buffer = bytearray()
        self.serial = 0
        self.outstanding = set()

    def send(self, op, fields):
        self.serial += 1
        payload = '{:id %d :op :%s%s}' % (self.serial, op, ''.join(
            ' :%s %s' % (key, json.dumps(value, ensure_ascii=False)) for key, value in fields.items()))
        payload = payload.encode('utf-8')
        if len(payload) > LIMIT:
            raise ValueError('request exceeds 4 MiB')
        self.socket.sendall(struct.pack('!I', len(payload)) + payload)
        self.outstanding.add(self.serial)
        return self.serial

    def read(self):
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
            text = self.buffer[4:4+size].decode('utf-8')
            del self.buffer[:4+size]
            match = re.match(r'^\{:id (\d+)\s', text)
            if match:
                self.outstanding.discard(int(match[1]))
            result.append(text)
        return result


def tools():
    result = []
    for name, description, properties, required in (
        ('eval', 'Evaluate Otium code in a running program.', {'code': {'type': 'string'}, 'ns': {'type': 'string'}}, ['code']),
        ('load', 'Reload a file form by form; definitions before an error remain installed.', {'path': {'type': 'string'}, 'ns': {'type': 'string'}}, ['path']),
        ('status', 'Report whether the program is running or paused.', {}, []),
        ('interrupt', 'Pause running Otium code at its next interrupt check.', {}, []),
        ('continue', 'Resume paused code with its state intact.', {}, []),
        ('abort', 'Abort paused code and run unwind-protect cleanup.', {}, []),
    ):
        result.append({'name': name, 'description': description,
                       'inputSchema': {'type': 'object', 'properties': properties,
                                       'required': required, 'additionalProperties': False}})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['attach', 'mcp'])
    parser.add_argument('path', nargs='?', type=Path)
    parser.add_argument('--socket', type=Path)
    parser.add_argument('--eval', dest='code')
    parser.add_argument('--ns', default='user')
    parser.add_argument('--op', choices=['status', 'interrupt', 'continue', 'abort'])
    args = parser.parse_args()
    connection = Connection(args.socket or args.path or default_socket())
    selector = selectors.DefaultSelector()
    selector.register(connection.socket, selectors.EVENT_READ, 'socket')
    one_shot = args.code is not None or args.op is not None
    if not one_shot:
        selector.register(sys.stdin, selectors.EVENT_READ, 'stdin')
    input_buffer = b""
    input_closed = False
    pending = {}
    descriptions = {tool['name']: tool for tool in tools()}

    def rpc(ident, result=None, error=None):
        value = {'jsonrpc': '2.0', 'id': ident}
        value['error' if error else 'result'] = error or result
        print(json.dumps(value, ensure_ascii=False), flush=True)

    if one_shot:
        connection.send(args.op or 'eval', {} if args.op else {'code': args.code, 'ns': args.ns})
    elif args.mode == 'attach':
        print('Connected. Enter Otium code, :load PATH, :ns NAME, :status, :continue or :abort. Ctrl-C pauses.', flush=True)
    try:
        while True:
            try:
                ready = selector.select()
            except KeyboardInterrupt:
                connection.send('interrupt', {})
                continue
            for key, _ in ready:
                if key.data == 'socket':
                    for text in connection.read():
                        match = re.match(r'^\{:id (\d+)\s', text)
                        if args.mode == 'mcp':
                            if match and int(match[1]) in pending:
                                ident, _ = pending.pop(int(match[1]))
                                rpc(ident, {'content': [{'type': 'text', 'text': text}],
                                            'isError': bool(re.match(r'^\{:id \d+ :ok #f', text))})
                            elif text.startswith('{:event :paused'):
                                for request_id, (ident, operation) in list(pending.items()):
                                    if operation in ('eval', 'load'):
                                        pending.pop(request_id)
                                        rpc(ident, {'content': [{'type': 'text', 'text':
                                            text + '\nEvaluation is suspended. Use eval to inspect state, then continue or abort.'}],
                                            'isError': False})
                                        break
                        else:
                            print(text, flush=True)
                            if one_shot and match:
                                return 1 if re.match(r'^\{:id \d+ :ok #f', text) else 0
                    if input_closed and not (pending if args.mode == 'mcp' else connection.outstanding):
                        return 0
                    continue
                chunk = os.read(sys.stdin.fileno(), 65536)
                if not chunk:
                    selector.unregister(sys.stdin)
                    input_closed = True
                    if not (pending if args.mode == 'mcp' else connection.outstanding):
                        return 0
                    continue
                input_buffer += chunk
                lines = input_buffer.split(b'\n')
                input_buffer = lines.pop()
                for raw_line in lines:
                    line = raw_line.decode('utf-8')
                    if args.mode == 'attach':
                        line = line.rstrip('\n')
                        if line.startswith(':ns '):
                            args.ns = line[4:]
                        elif line.startswith(':load '):
                            connection.send('load', {'path': line[6:], 'ns': args.ns})
                        elif line in (':interrupt', ':continue', ':abort', ':status'):
                            connection.send(line[1:], {})
                        elif line.strip():
                            connection.send('eval', {'code': line, 'ns': args.ns})
                        continue
                    ident = None
                    try:
                        message = json.loads(line)
                        ident = message.get('id')
                        method = message.get('method')
                        if ident is None:
                            continue
                        if method == 'initialize':
                            rpc(ident, {'protocolVersion': message.get('params', {}).get('protocolVersion', '2024-11-05'),
                                        'capabilities': {'tools': {}},
                                        'serverInfo': {'name': 'otium-dev', 'version': '0.1.0'}})
                        elif method == 'ping':
                            rpc(ident, {})
                        elif method == 'tools/list':
                            rpc(ident, {'tools': tools()})
                        elif method == 'tools/call':
                            params = message['params']
                            name = params['name']
                            fields = params.get('arguments', {})
                            schema = descriptions[name]['inputSchema']
                            if (not isinstance(fields, dict) or set(fields) - schema['properties'].keys()
                                    or not set(schema['required']) <= fields.keys()
                                    or any(not isinstance(value, str) for value in fields.values())):
                                raise ValueError('invalid tool arguments')
                            pending[connection.send(name, fields)] = (ident, name)
                        else:
                            rpc(ident, error={'code': -32601, 'message': 'unknown method'})
                    except (ValueError, KeyError, TypeError, AttributeError) as error:
                        rpc(ident, error={'code': -32602, 'message': str(error)})
    finally:
        selector.close()
        connection.socket.close()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, EOFError, ValueError) as error:
        print('otium: ' + str(error), file=sys.stderr)
        sys.exit(1)
