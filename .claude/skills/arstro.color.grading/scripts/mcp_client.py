#!/usr/bin/env python3
"""A minimal MCP stdio client: start a server, handshake, call tools, save images to files."""
import base64, json, subprocess, sys, os

class Client:
    def __init__(self, argv, log=None):
        self.p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=open(log or os.devnull, 'w'), text=True, bufsize=1)
        self.id = 0
        r = self.req('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {},
                                    'clientInfo': {'name': 'mcpc', 'version': '0'}})
        self.info = r
        self.send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
    def send(self, m):
        self.p.stdin.write(json.dumps(m) + '\n'); self.p.stdin.flush()
    def req(self, method, params=None):
        self.id += 1
        self.send({'jsonrpc': '2.0', 'id': self.id, 'method': method, 'params': params or {}})
        line = self.p.stdout.readline()
        if not line: raise RuntimeError('server closed')
        r = json.loads(line)
        if 'error' in r: raise RuntimeError(r['error'])
        return r['result']
    def tools(self):
        return self.req('tools/list')['tools']
    def call(self, name, img=None, **args):
        r = self.req('tools/call', {'name': name, 'arguments': args})
        texts = []
        for c in r['content']:
            if c['type'] == 'text': texts.append(c['text'])
            elif c['type'] == 'image' and img:
                open(img, 'wb').write(base64.b64decode(c['data'])); texts.append('[image -> %s]' % img)
        return r['isError'], '\n'.join(texts)
    def close(self):
        self.p.stdin.close(); self.p.wait(timeout=30)
