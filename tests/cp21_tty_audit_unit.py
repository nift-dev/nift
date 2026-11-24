"""Only a witnessed child handoff can establish CP21 terminal ownership."""
import tempfile
from pathlib import Path
import cp21_interactive_host as c

checks = 0
with tempfile.TemporaryDirectory() as td:
    p = Path(td) / 'audit'
    for content in [None, '', 'handoff 10 10 11 11 0\n',
                    'ready 10 10 0 -1 0\n', 'ready 20 20 0 20 0\n',
                    'ready 10 10 0 10 0\nbroken\n',
                    'ready 10 10 0 10 0\nhandoff x 10 11 11 0\n']:
        if content is not None:
            p.write_text(content)
        try:
            c.read_tty_audit(p, 10)
        except (OSError, ValueError):
            checks += 1
        else:
            raise AssertionError(('invalid prerequisite accepted', content))
    ready = 'ready 10 10 0 10 0\n'
    for suffix in ['', 'handoff 10 10 10 10 0\n',
                   'handoff 10 10 11 10 -1\n', 'handoff 10 10 11 10 0\n',
                   'handoff 20 20 21 21 0\n', 'handoff 10 10 -1 -1 0\n']:
        p.write_text(ready + suffix)
        assert not c.background_tty_stolen(c.read_tty_audit(p, 10)), suffix
        checks += 1
    handoff = 'handoff 10 10 11 11 0\n'
    stopped = 'stopped 10 10 11 11 0\n'
    restore = 'handoff 10 10 10 10 0\n'
    for sequence in [ready, ready + handoff, ready + handoff + stopped + restore,
                     ready + stopped + 'input 10 10 0 11 0\n']:
        p.write_text(sequence)
        assert c.audit_shell_restored(c.read_tty_audit(p, 10)) is None
        checks += 1
    for input_owner, expected, suffix in [(10, True, ''), (11, False, restore)]:
        p.write_text(ready + handoff + stopped + f'input 10 10 0 {input_owner} 0\n' + suffix)
        records = c.read_tty_audit(p, 10)
        assert c.background_tty_stolen(records)
        assert c.audit_shell_restored(records) is expected
        checks += 2
    p.write_text(ready + handoff + stopped + 'input 10 10 0 -1 0\n')
    try:
        c.audit_shell_restored(c.read_tty_audit(p, 10))
    except OSError:
        checks += 1
    else:
        raise AssertionError('invalid tty owner accepted')
print('PASS CP21 terminal audit evidence guards:', checks)
