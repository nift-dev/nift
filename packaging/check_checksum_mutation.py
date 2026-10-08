#!/usr/bin/env python3
"""Protect the installer smoke's negative checksum from accidental no-op input."""
from pathlib import Path
import re
import subprocess
source = Path(__file__).with_name('installer-smoke.sh').read_text()
match = re.search(r'^corrupt_checksum\(\) \{\n.*?^\}', source, re.M | re.S)
assert match, 'actual checksum corruption helper missing'
# All possible first bytes include the old 00 no-op failure. Test the actual
# helper, requiring a changed, syntactically valid digest and preserved filename.
lines = [f'{prefix:02x}' + 'a' * 62 + '  nift-embed-linux-arm64.tar.gz' for prefix in range(256)]
result = subprocess.run(['bash', '-c', match[0] + '\ncorrupt_checksum'],
                        input='\n'.join(lines)+'\n', text=True, capture_output=True)
assert result.returncode == 0, result.stderr
changed = result.stdout.splitlines()
assert len(changed) == len(lines)
for original, mutated in zip(lines, changed):
    before_hash, before_name = original.split()
    after_hash, after_name = mutated.split()
    assert re.fullmatch('[0-9a-f]{64}', after_hash), 'invalid mutated digest'
    assert after_hash != before_hash, 'negative checksum mutation did not change the digest'
    assert after_name == before_name, 'mutation changed the archive selection'
print('PASS: negative installer checksum changes all 256 possible digest prefixes')
