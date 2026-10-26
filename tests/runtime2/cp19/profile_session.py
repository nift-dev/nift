#!/usr/bin/env python3
"""Matched actual-PTY instruction profiles; startup is a separate subtraction.

Run under the shared heavy lock. This records instructions, never timings.
The base and VM-off controls execute legacy; only candidate require is VM.
"""
import argparse, hashlib, json, os, pty, select, subprocess, termios, time
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--base', required=True)
p.add_argument('--nift', required=True)
p.add_argument('--switch', required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--units', type=int, default=1000)
a = p.parse_args()
a.out = a.out.resolve()
a.out.mkdir(parents=True, exist_ok=True)

def prompt(fd, after=False):
    data = b''
    deadline = time.monotonic() + 60
    while not (data.endswith(b'$ ') and (not after or b'\r\n' in data)):
        left = deadline - time.monotonic()
        if left <= 0 or not select.select([fd], [], [], left)[0]:
            raise TimeoutError(repr(data))
        chunk = os.read(fd, 65536)
        if not chunk:
            raise RuntimeError('closed PTY: ' + repr(data))
        data += chunk
    return data.decode(errors='replace')

rows = []
for name, binary, mode in [('base', a.base, 'require'), ('legacy-control', a.nift, '0'),
                           ('default', a.nift, 'require'), ('switch', a.switch, 'require')]:
    binary = Path(binary).resolve()
    for units in [0, a.units]:
        folder = a.out / f'{name}-{units}'
        folder.mkdir()
        stats = folder / 'stats.jsonl'
        env = {k: v for k, v in os.environ.items() if not k.startswith('NIFT_')}
        env.update(HOME=str(folder), NIFT_VM=mode, NIFT_VM_STATS=str(stats), NO_COLOR='1', TERM='dumb')
        command = ['valgrind', '--tool=callgrind', '--dump-instr=yes',
                   '--callgrind-out-file=' + str(folder / 'callgrind.out'), str(binary), '-i']
        master, slave = pty.openpty()
        attrs = termios.tcgetattr(slave)
        attrs[3] &= ~termios.ECHO
        termios.tcsetattr(slave, termios.TCSANOW, attrs)
        with (folder / 'profiler.txt').open('w') as err:
            proc = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=err,
                                    cwd=folder, env=env)
            os.close(slave)
            transcript = ''
            try:
                transcript += prompt(master)
                os.write(master, b'x:=0\n')
                transcript += prompt(master, True)
                for i in range(1, units + 1):
                    os.write(master, b'(x+=1)\n')
                    reply = prompt(master, True)
                    transcript += reply
                    lines = reply.split('\r\n')
                    assert len(lines) == 3 and lines[1] == str(i) and lines[0].endswith('(x+=1)'), reply
                os.write(master, b'exit\n')
                proc.wait(timeout=60)
                assert proc.returncode == 0, proc.returncode
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                os.close(master)
                (folder / 'transcript.txt').write_text(transcript)
        counters = [json.loads(s) for s in stats.read_text().splitlines()] if stats.exists() else []
        if mode == 'require' and name != 'base':
            assert len(counters) == units + 1 and all(r['vm_executions'] == 1 and
                r['legacy_executions'] == 0 and r['outcome'] == 'vm' for r in counters)
        annotation = subprocess.check_output(['callgrind_annotate', '--threshold=100',
            '--inclusive=no', '--auto=no', str(folder / 'callgrind.out')], text=True)
        (folder / 'self.txt').write_text(annotation)
        rows.append(dict(name=name, units=units, mode=mode, command=command,
            binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), stats=counters))
        (a.out / 'runs.json').write_text(json.dumps(rows, indent=2) + '\n')
        print('PASS', name, units, flush=True)
