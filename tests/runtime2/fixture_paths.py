"""Canonicalize only the temporary directory owned by a test harness."""
import os
import re


def _replace_root(data, root, replacement, strip):
    root = os.fspath(root)
    spellings = {root, os.path.realpath(root)}
    if os.name == "nt":
        spellings |= {name.replace("\\", "/") for name in spellings}
    for name in sorted(spellings, key=len, reverse=True):
        separator = "/" if os.name != "nt" or "/" in name else "\\"
        pattern = r"(?<![\w./\\:-])" + re.escape(name)
        if strip:
            pattern += re.escape(separator)
        else:
            pattern += r"""(?=$|[\s'":/\\])"""
        if isinstance(data, bytes):
            data = re.sub(pattern.encode(), lambda _: replacement.encode(), data)
        else:
            data = re.sub(pattern, lambda _: replacement, data)
    return data


def replace_fixture_root(data, root, replacement):
    return _replace_root(data, root, replacement, strip=False)


def strip_fixture_root(data, root):
    return _replace_root(data, root, "", strip=True)
