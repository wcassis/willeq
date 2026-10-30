#!/usr/bin/env python3
"""B1214: send MQ2 commands over the MQ2Telnet plugin and capture the echoed chat.

usage: mqtel.py [--host H] [--port P] [--wait S] [--log FILE] CMD [CMD ...]
Each CMD is sent as one line (a slash command or a /macro invocation); a CMD of
the form `@sleep:<sec>` pauses. Everything the plugin sends back (the MQ2 chat
stream: /echo output, macro output, client chat) is printed with a UTC
timestamp per line and appended to --log.
"""
import argparse, socket, sys, time, datetime

ap = argparse.ArgumentParser()
ap.add_argument('--host', default='127.0.0.1')
ap.add_argument('--port', type=int, default=2323)
ap.add_argument('--user', default='b1214')
ap.add_argument('--password', default='spike')
ap.add_argument('--wait', type=float, default=3.0, help='seconds to keep reading after the last command')
ap.add_argument('--log')
ap.add_argument('cmds', nargs='*')
a = ap.parse_args()

log = open(a.log, 'a') if a.log else None
buf = b''

def stamp():
    return datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%S.%f')[:-3] + 'Z'

def emit(line):
    s = f'{stamp()} {line}'
    print(s, flush=True)
    if log:
        log.write(s + '\n'); log.flush()

def pump(s, secs):
    global buf
    end = time.time() + secs
    while time.time() < end:
        s.settimeout(max(0.05, end - time.time()))
        try:
            d = s.recv(65536)
        except socket.timeout:
            break
        if not d:
            emit('<eof>'); return False
        d = bytes(c for c in d if c != 0)
        buf += d
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            t = line.decode('latin-1').rstrip('\r')
            # strip telnet IAC sequences
            t = ''.join(ch for ch in t if ord(ch) < 0xF0)
            if t.strip():
                emit('< ' + t)
    return True

s = socket.create_connection((a.host, a.port), timeout=10)
pump(s, 1.5)
s.sendall(a.user.encode() + b'\r\n'); pump(s, 1.0)
s.sendall(a.password.encode() + b'\r\n'); pump(s, 1.5)
for c in a.cmds:
    if c.startswith('@sleep:'):
        pump(s, float(c.split(':', 1)[1])); continue
    emit('> ' + c)
    s.sendall(c.encode('latin-1') + b'\r\n')
    pump(s, 0.3)
pump(s, a.wait)
s.close()
