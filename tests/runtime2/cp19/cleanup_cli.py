"""Independent job/script cleanup on every interactive shell exit (CP19 R1 F1)."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import signal
import time

p = argparse.ArgumentParser()
p.add_argument('--nift', required=True)
p.add_argument('--fault', required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
spec = importlib.util.spec_from_file_location('pty_contract', Path(__file__).resolve().parents[2] / 'cp21_interactive_host.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
rows = []
a.out.parent.mkdir(parents=True, exist_ok=True)
for mode in ['default', '0', '1', 'require']:
    for end in ['eof', 'exit', 'exit 7']:
        for case in ['normal', 'file', 'background-file', 'stopped-file', 'both', 'job']:
            env = {}
            for key in list(os.environ):
                if key.startswith('NIFT_'):
                    os.environ.pop(key)
            if mode != 'default':
                env['NIFT_VM'] = mode
            else:
                # Pty merges the environment; explicitly remove inherited mode.
                os.environ.pop('NIFT_VM', None)
            env['NIFT_VM_STATS'] = 'vm-stats.jsonl'
            fault = case in ['both', 'job']
            if fault:
                prefix = os.environ.get('CP19_ASAN_PRELOAD', '')
                env['LD_PRELOAD'] = (prefix + ':' if prefix else '') + str(Path(a.fault).resolve())
            s = c.Pty(str(Path(a.nift).resolve()), env)
            row = dict(mode=mode, end=end, case=case)
            try:
                c.initial_prompt(s)
                path = Path(s.root) / 'managed'
                path.write_text('original')
                file_error = case in ['file', 'background-file', 'stopped-file', 'both']
                if file_error:
                    s.line('f:=file("managed");f.open("rw");f.write("dirty")')
                    s.prompt()
                has_job = case in ['background-file', 'stopped-file', 'both', 'job']
                if has_job:
                    command = "sh -c 'echo $$ > child.pid; "
                    if case == 'background-file':
                        command += "exec sleep 60' &"
                    else:
                        command += "kill -STOP $$; exec sleep 60'"
                    s.line(command)
                    s.prompt()
                    s.until(lambda: (Path(s.root) / 'child.pid').exists(), what='child identity')
                before = len(s.buf)
                if end == 'eof':
                    os.write(s.fd, b'\x04')
                else:
                    s.line(end)
                    before = 0
                deadline = time.monotonic() + 10
                status = None
                while time.monotonic() < deadline:
                    s.read_for(0.025)
                    got, raw_status = os.waitpid(s.pid, os.WNOHANG)
                    if got:
                        status = os.waitstatus_to_exitcode(raw_status)
                        break
                text = s.clean(s.buf[before:])
                stats_path = Path(s.root) / 'vm-stats.jsonl'
                stats = [json.loads(line) for line in stats_path.read_text().splitlines()] if stats_path.exists() else []
                row.update(status=status, output=text, entire_output=s.clean(s.transcript+s.buf), stats=stats)
                rows.append(row)
                a.out.write_text(json.dumps(rows, indent=2)+'\n')
                assert not any(marker in row['entire_output'] for marker in
                               ['ERROR: AddressSanitizer', 'ERROR: LeakSanitizer',
                                'AddressSanitizer:DEADLYSIGNAL', 'runtime error:']), row
                assert status is not None, row
                if mode in ['1', 'require'] and file_error:
                    assert stats and sum(x['vm_executions'] for x in stats) >= 1, row
                    assert all(x['legacy_executions'] == 0 for x in stats), row
                # Accepted VM cancellation (nonzero explicit exit) discards unsaved
                # resources without an unclosed-file Error; legacy reports it.
                reports_file = file_error and not (mode in ['1', 'require'] and end == 'exit 7')
                assert text.count('managed file left open') == int(reports_file), row
                assert text.count('failed to restore terminal') == int(fault), row
                if reports_file and fault and mode in ['default', '0']:
                    assert text.index('managed file left open') < text.index('failed to restore terminal'), row
                # A requested nonzero shell status survives cleanup diagnostics.
                expected = 7 if end == 'exit 7' else int(file_error or fault)
                assert status == expected, row
                assert path.read_text() == 'original', row
                if has_job:
                    pid = int((Path(s.root) / 'child.pid').read_text())
                    try:
                        os.kill(pid, 0)
                    except ProcessLookupError:
                        pass
                    else:
                        raise AssertionError(('child not reaped', pid, row))
            finally:
                s.close()
print('PASS CP19 CLI cleanup: 72 EOF/exit/status, normal/independent/simultaneous faults, stopped/background cases')
