#!/usr/bin/env python3
"""vitactl: talks to the dev build of Vitalatro (VL_DEV, title VLTR00002) over the network.

    python tools/vitactl.py [--ip IP] <command> [args...]
    python tools/vitactl.py [--ip IP] do "<command> args" "<command> args" ...   (one connection)

Commands (answered by src/dev/devserver.cpp):
    ping | state | log                    info, game state, log lines since the last `log`
    perf [seconds]                        fps, CPU ms per phase, GPU estimate (default 2 s)
    bench                                 the GPU bench (every mode, vsync off); returns its log
    wait <frames>                         answers after that many frames
    input "<f>:<button> <f>:touch:<x>:<y>"  scripted input, frames counted from now; answers when done
    shot <out.png>                        screenshot of the next frame
    set <key> <value> | get <key>         G.SETTINGS (also set vsync/bench/occlusion)
    run [seed] [stake] | menu | debugtable | restart
    shader <name>... | shader --changed   push compiled shaders (build/assets_vita/shaders/<name>.gxp)
                                          and rebuild the GPU programs, no reinstall
    put <local> <remote> | fetch <remote> <local>   files (remote paths like ux0:data/...)

The address comes from --ip, $VITA_IP, build/vita_ip.txt, else 192.168.68.100. Port 7777.
"""
import hashlib
import json
import os
import shlex
import socket
import struct
import sys
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 7777
SHADER_DIR = os.path.join(ROOT, 'build', 'assets_vita', 'shaders')
SHADER_CACHE = os.path.join(ROOT, 'build', 'vitactl_shaders.json')


def default_ip():
    if os.environ.get('VITA_IP'):
        return os.environ['VITA_IP']
    p = os.path.join(ROOT, 'build', 'vita_ip.txt')
    if os.path.exists(p):
        return open(p).read().strip()
    return '192.168.68.100'


class Conn:
    def __init__(self, ip):
        self.s = socket.create_connection((ip, PORT), timeout=10)
        self.s.settimeout(None)
        self.buf = b''

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.s.recv(1 << 20)
            if not chunk:
                raise ConnectionError('connection closed')
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def _line(self):
        while b'\n' not in self.buf:
            chunk = self.s.recv(1 << 16)
            if not chunk:
                raise ConnectionError('connection closed')
            self.buf += chunk
        line, self.buf = self.buf.split(b'\n', 1)
        return line.decode()

    def call(self, cmd, payload=b''):
        self.s.sendall(f'{len(payload)} {cmd}\n'.encode() + payload)
        status, n = self._line().split(' ')
        data = self._read(int(n))
        return status == 'OK', data


def write_png(path, w, h, rgba):
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    open(path, 'wb').write(png)


def shader_payload(names):
    out = b''
    for n in names:
        data = open(os.path.join(SHADER_DIR, n + '.gxp'), 'rb').read()
        out += f'{n} {len(data)}\n'.encode() + data
    return out


def changed_shaders():
    cache = json.load(open(SHADER_CACHE)) if os.path.exists(SHADER_CACHE) else {}
    now, changed = {}, []
    for f in sorted(os.listdir(SHADER_DIR)):
        if f.endswith('.gxp'):
            h = hashlib.sha1(open(os.path.join(SHADER_DIR, f), 'rb').read()).hexdigest()
            now[f[:-4]] = h
            if cache.get(f[:-4]) != h:
                changed.append(f[:-4])
    return changed, now


def run(conn, argv):
    cmd, args = argv[0], argv[1:]
    t0 = time.time()
    if cmd == 'shot':
        ok, data = conn.call('shot')
        if not ok:
            return False, data.decode()
        head, px = data.split(b'\n', 1)
        w, h = map(int, head.split())
        out = args[0] if args else 'vita_shot.png'
        write_png(out, w, h, px)
        return True, f'{out} ({w}x{h}, {time.time() - t0:.1f} s)'
    if cmd == 'shader':
        if args == ['--changed']:
            names, now = changed_shaders()
            if not names:
                return True, 'no shader changed'
        else:
            names, now = args, None
        ok, data = conn.call('shader', shader_payload(names))
        if ok and now is not None:
            json.dump(now, open(SHADER_CACHE, 'w'), indent=0)
        return ok, data.decode() + (' [' + ' '.join(names) + ']' if ok else '')
    if cmd == 'put':
        ok, data = conn.call('put ' + args[1], open(args[0], 'rb').read())
        return ok, data.decode() or f'{args[0]} -> {args[1]} ({time.time() - t0:.1f} s)'
    if cmd == 'fetch':
        ok, data = conn.call('fetch ' + args[0])
        if ok:
            open(args[1], 'wb').write(data)
            return True, f'{args[0]} -> {args[1]} ({len(data)} bytes)'
        return False, data.decode()
    ok, data = conn.call(' '.join([cmd] + args))
    return ok, data.decode(errors='replace')


def main():
    argv = sys.argv[1:]
    ip = default_ip()
    if len(argv) >= 2 and argv[0] == '--ip':
        ip, argv = argv[1], argv[2:]
    if not argv:
        print(__doc__)
        return 1
    try:
        conn = Conn(ip)
    except OSError as e:
        print(f'cannot reach the dev build at {ip}:{PORT}: {e}')
        return 2
    cmds = [shlex.split(c) for c in argv[1:]] if argv[0] == 'do' else [argv]
    for c in cmds:
        ok, out = run(conn, c)
        if out.strip():
            print(out.rstrip())
        if not ok:
            print(f'ERR ({" ".join(c)})')
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
