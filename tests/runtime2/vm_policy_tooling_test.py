"""Missing independent AST tooling must fail infrastructure before comparison."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ('vm_adversarial.py', 'vm_parity.py', 'vm_engine_selection.py',
           'vm_former_replay.py', 'vm_d018.py', 'vm_chain.py', 'closures_s1.py',
           'vm_globals.py', 'vm_modules.py',
           'vm_policy_negative.py', 'vm_review_cp12.py', 'vm_structs.py')
with tempfile.TemporaryDirectory() as directory:
    missing = str(Path(directory) / 'missing-parser')
    for script in SCRIPTS:
        result = subprocess.run([sys.executable, str(ROOT / 'tests/runtime2' / script), '--help'],
                                env={**os.environ, 'VM_PARSE_DUMP': missing}, capture_output=True)
        assert result.returncode == 2, (script, result)
        assert b'HARNESS INFRASTRUCTURE FAILURE: D-018c' in result.stderr, (script, result)
        assert missing.encode() in result.stderr, (script, result)
        assert not result.stdout, (script, result)
    result = subprocess.run([sys.executable, str(ROOT / 'tools/rt-diff/rt-diff'), '--help'],
                            env={**os.environ, 'VM_PARSE_DUMP': missing}, capture_output=True)
    assert result.returncode == 2 and b'HARNESS INFRASTRUCTURE FAILURE' in result.stderr, result
    result = subprocess.run([sys.executable, '-c',
                             'import vm_sites; vm_sites.parse(b"x := 1;")'],
                            cwd=ROOT / 'tests/runtime2',
                            env={**os.environ, 'VM_PARSE_DUMP': missing}, capture_output=True)
    assert result.returncode == 2 and b'HARNESS INFRASTRUCTURE FAILURE' in result.stderr, result
print(f'PASS parser tooling: {len(SCRIPTS)} direct-script negatives + rt-diff negative + direct oracle negative')
