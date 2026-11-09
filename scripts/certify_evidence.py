"""C42 (BLOCKERS B21, owner 2026-11-09 16:46): certification evidence lives outside every tree.

Linode instance 110461664: test-build-reuse's `make distclean` inside the certified checkout removed
.build/certify (every per-gate log, the preflight) and classification crashed. Evidence now goes to
<evidence-root>/<campaign-id>/ (default /var/tmp/nift-certify), which must not lie inside the
checkout, a baseline or oracle tree, a user copy or the work root of isolated gate copies.

Layout:
  identity.json                  the campaign identity (written once)
  summary.json, summary.txt      the latest invocation's
  invocations/<N>/               each invocation's preflight.json, build-nift.log, prebuild.log,
                                 oracle-recheck.json, summary.json, summary.txt
  gates/<target>/run.log, run.json            first pass: written once, fsynced, read-only
  gates/<target>/cls-<N>/...                  classification attempt N: its own logs and records,
                                              class.json last (an attempt without one was interrupted
                                              and is kept as it is; a resume starts attempt N+1)
Every run record carries the campaign id, the identity's sha256, the tree it ran in (commit, tree,
working-tree diff, nift sha256, before and after), the toolchain and the environment.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

DEFAULT_ROOT = "/var/tmp/nift-certify"
DEFAULT_WORK = "/var/tmp/nift-certify-work"
SCHEMA_IDENTITY = "nift-certify-identity/1"
SCHEMA_RUN = "nift-certify-run/1"
SCHEMA_CLASS = "nift-certify-class/1"
_GIT = dict(os.environ, GIT_OPTIONAL_LOCKS="0")


def sha256_file(path):
    try:
        h = hashlib.sha256()
        with open(path, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return None


def fsync_dir(d):
    try:
        fd = os.open(str(d), os.O_RDONLY)
    except OSError:
        return
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def write_durable(path, data, mode=0o444, exclusive=True):
    """Write `data` (bytes) to `path` atomically and durably: a temporary file in the same
    directory, fsync, rename (refused when `exclusive` and the path exists: evidence is never
    rewritten), fsync of the directory."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if exclusive and path.exists():
        raise FileExistsError(f"evidence is written once: {path} exists")
    tmp = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    with open(tmp, "wb") as fh:
        fh.write(data)
        fh.flush()
        os.fsync(fh.fileno())
    os.chmod(tmp, mode)
    if exclusive:
        os.link(tmp, path)          # fails if the path appeared meanwhile
        os.unlink(tmp)
    else:
        os.replace(tmp, path)
    fsync_dir(path.parent)


def write_json(path, obj, **kw):
    write_durable(path, (json.dumps(obj, indent=2, sort_keys=True, default=list) + "\n").encode(), **kw)


def read_json(path):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return None


def fsync_file(path):
    try:
        fd = os.open(str(path), os.O_RDONLY)
    except OSError:
        return
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def inside(child, parent):
    c, p = os.path.realpath(child), os.path.realpath(parent)
    return c == p or c.startswith(p.rstrip(os.sep) + os.sep)


def root_problems(evidence_dir, work_dir, protected):
    """The evidence and work directories must not lie inside any protected tree (name -> path),
    and neither inside the other: a gate, `make clean`, `make distclean` or the removal of an
    isolated copy could delete them."""
    out = []
    for label, d in (("evidence", evidence_dir), ("work", work_dir)):
        for name, tree in protected.items():
            if tree and inside(d, tree):
                out.append(f"the {label} directory {d} is inside the {name} ({tree}): a gate, make clean or "
                           f"make distclean there could delete it (--evidence-root / --work-root)")
    if work_dir and inside(evidence_dir, work_dir):
        out.append(f"the evidence directory {evidence_dir} is inside the work root {work_dir} (isolated copies are removed)")
    if work_dir and inside(work_dir, evidence_dir):
        out.append(f"the work root {work_dir} is inside the evidence directory {evidence_dir}")
    return out


def tree_identity(path):
    """commit, tree, working-tree diff and nift sha256 of a checkout (read-only git)."""
    p = Path(path)
    def git(*a):
        r = subprocess.run(["git", "-c", "safe.directory=*", "-C", str(p)] + list(a),
                           capture_output=True, env=_GIT)
        return r.stdout if r.returncode == 0 else None
    head, tree = git("rev-parse", "HEAD"), git("rev-parse", "HEAD^{tree}")
    diff = git("diff", "HEAD", "--binary")
    return {"path": os.path.realpath(p),
            "commit": head.decode().strip() if head else None,
            "tree": tree.decode().strip() if tree else None,
            "diff_sha256": hashlib.sha256(diff).hexdigest() if diff else (None if diff is None else "clean"),
            "nift_sha256": sha256_file(p / "nift")}


SAME_TREE_KEYS = ("commit", "tree", "diff_sha256", "nift_sha256")


def tree_mismatch(got, want):
    """[] when two tree identities are the same source and binary; otherwise the differences."""
    if not want:
        return [f"no expected identity (got {(got or {}).get('commit')})"]
    return [f"{k} {str((got or {}).get(k))[:16]} != {str(want.get(k))[:16]}"
            for k in SAME_TREE_KEYS if (got or {}).get(k) != want.get(k)]


def identity_sha256(identity):
    core = {k: v for k, v in identity.items() if k not in ("campaign_id", "created", "schema")}
    core["candidate"] = {k: v for k, v in (core.get("candidate") or {}).items() if k != "path"}
    return hashlib.sha256(json.dumps(core, sort_keys=True, default=list).encode()).hexdigest()


def identity_diff(a, b, prefix=""):
    """The differing leaves of two identities (paths aside)."""
    out = []
    keys = sorted(set(a or {}) | set(b or {}))
    for k in keys:
        if k in ("campaign_id", "created", "schema", "path"):
            continue
        x, y = (a or {}).get(k), (b or {}).get(k)
        if isinstance(x, dict) and isinstance(y, dict):
            out += identity_diff(x, y, prefix + k + ".")
        elif x != y:
            out.append(f"{prefix}{k}: {str(x)[:60]} != {str(y)[:60]}")
    return out


def safe_name(name):
    return re.sub(r"[^\w.+-]", "_", name)


class Store:
    def __init__(self, root, campaign_id):
        if not re.match(r"^[\w.+-]+$", campaign_id or ""):
            raise ValueError(f"campaign id {campaign_id!r}: letters, digits, '.', '+', '-', '_' only")
        self.root = Path(root)
        self.campaign_id = campaign_id
        self.dir = self.root / campaign_id
        self.identity = None

    def open(self, identity, resume):
        """Create the campaign directory, or (resume) reopen it. Returns [] or the problems: a
        campaign directory is never reused without --resume, and never resumed under another
        identity."""
        idf = self.dir / "identity.json"
        if self.dir.exists() and not resume:
            return [f"the campaign directory {self.dir} exists: pass --resume to continue it, or another --campaign-id"]
        if resume:
            old = read_json(idf)
            if old is None:
                return [f"cannot resume: {idf} is missing or unreadable"]
            if old.get("identity_sha256") != identity_sha256(identity):
                return [f"cannot resume campaign {self.campaign_id}: its identity differs from this run's "
                        f"(source tree, binary, toolchain, environment or configuration): "
                        + "; ".join(identity_diff(old, identity)[:12])]
            self.identity = old
            return []
        self.dir.mkdir(parents=True, exist_ok=False)
        os.chmod(self.dir, 0o755)
        ident = dict(identity, schema=SCHEMA_IDENTITY, campaign_id=self.campaign_id, created=time.time(),
                     identity_sha256=identity_sha256(identity))
        write_json(idf, ident)
        self.identity = ident
        return []

    def path(self, name):
        return self.dir / name

    def new_invocation(self):
        """invocations/<N>: this invocation's preflight, build logs and summary (a resume adds one)"""
        d = self.dir / "invocations"
        d.mkdir(exist_ok=True)
        n = 1 + max([int(x.name) for x in d.iterdir() if x.name.isdigit()], default=0)
        inv = d / f"{n:03d}"
        inv.mkdir()
        fsync_dir(d)
        return inv

    def gate_dir(self, target):
        d = self.dir / "gates" / safe_name(target)
        d.mkdir(parents=True, exist_ok=True)
        return d

    def first_log(self, target):
        return self.gate_dir(target) / "run.log"

    def first_record(self, target):
        return read_json(self.gate_dir(target) / "run.json")

    def new_attempt(self, target):
        d = self.gate_dir(target)
        n = 1 + max([int(m.group(1)) for x in d.iterdir() if (m := re.match(r"^cls-(\d+)$", x.name))], default=0)
        a = d / f"cls-{n}"
        a.mkdir()
        fsync_dir(d)
        return a

    def completed_class(self, target):
        d = self.dir / "gates" / safe_name(target)
        if not d.is_dir():
            return None
        done = sorted((int(m.group(1)), x) for x in d.iterdir()
                      if (m := re.match(r"^cls-(\d+)$", x.name)) and (x / "class.json").is_file())
        return read_json(done[-1][1] / "class.json") if done else None

    def attempts(self, target):
        d = self.dir / "gates" / safe_name(target)
        return sorted(x.name for x in d.iterdir() if x.name.startswith("cls-")) if d.is_dir() else []

    def write_class(self, attempt_dir, target, result):
        write_json(Path(attempt_dir) / "class.json",
                   {"schema": SCHEMA_CLASS, "campaign_id": self.campaign_id, "target": target,
                    "identity_sha256": (self.identity or {}).get("identity_sha256"), "result": result,
                    "time": time.time()})


def first_pass_problem(store, target, rec):
    """Why a first-pass record cannot be classified, or None: missing or altered evidence
    (EVIDENCE-LOST), or a record from another campaign or identity (IDENTITY-MISMATCH)."""
    log = store.first_log(target)
    if rec is None:
        return "EVIDENCE-LOST", f"no first-pass record {store.gate_dir(target) / 'run.json'}"
    if not log.is_file():
        return "EVIDENCE-LOST", f"the first-pass log {log} is missing"
    if sha256_file(log) != rec.get("log_sha256"):
        return "EVIDENCE-LOST", f"the first-pass log {log} differs from its record (sha256)"
    if rec.get("campaign_id") != store.campaign_id:
        return "IDENTITY-MISMATCH", f"the record is from campaign {rec.get('campaign_id')}, not {store.campaign_id}"
    if rec.get("identity_sha256") != (store.identity or {}).get("identity_sha256"):
        return "IDENTITY-MISMATCH", "the record was made under another campaign identity"
    return None


def isolated_copy(src, work_root, target, user=None):
    """C42: a private copy of `src` for a destructive gate (cp -a, the build state included)."""
    base = Path(work_root) / f"{safe_name(target)}-{os.getpid()}-{time.time_ns()}"
    base.mkdir(parents=True)
    dest = base / Path(os.path.realpath(src)).name
    subprocess.run(["cp", "-a", str(src), str(dest)], check=True)
    if user:
        os.chmod(base, 0o755)
        subprocess.run(["chown", "-R", f"{user}:", str(dest)], check=True)
    return base, dest


def keep_from_copy(copy, keep, dest):
    """Copy a destructive gate's own output paths out of its copy into the evidence."""
    kept = []
    for rel in keep:
        p = Path(copy) / rel
        if p.exists():
            target = Path(dest) / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            if p.is_dir():
                shutil.copytree(p, target, symlinks=True, dirs_exist_ok=True)
            else:
                shutil.copy2(p, target)
            kept.append(rel)
    return kept
