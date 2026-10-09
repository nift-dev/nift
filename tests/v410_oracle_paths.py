"""Normalize only the deliberate oracle fixture root, including MSYS2 mapping."""
import os
from pathlib import Path
import re
import subprocess
import sys


def fixture_root_aliases(root):
    aliases = {str(root), root.as_posix()}
    if os.environ.get('MSYSTEM') or sys.platform in ('msys', 'cygwin'):
        native = subprocess.run(['cygpath', '-m', str(root)], capture_output=True,
                                text=True, encoding='utf-8', check=True).stdout.strip()
        if not native:
            raise RuntimeError('cygpath returned an empty fixture root')
        aliases.update((native, native.replace('/', '\\')))
    return sorted(aliases, key=len, reverse=True)


def normalize_fixture_paths(text, aliases):
    for alias in sorted(set(aliases), key=len, reverse=True):
        # A POSIX suffix inside an unrelated/native path is not a root match.
        pattern = r'(?<![\w./\\:-])' + re.escape(alias) + r'[/\\]'
        text = re.sub(pattern, '<ORACLE_ROOT>/', text)
    return text
