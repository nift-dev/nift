#!/usr/bin/env python3
from __future__ import annotations
import argparse, platform, json, os, pathlib, signal, subprocess, tempfile, time

ap=argparse.ArgumentParser()
ap.add_argument("--nift",required=True)
ap.add_argument("--cycles",type=int,default=500)
ap.add_argument("--interval",type=float,default=0.22)
ap.add_argument("--output",required=True)
ap.add_argument("--shutdown-grace-seconds",type=float,default=30.0,
                help="Grace period after SIGINT so supervisors such as Valgrind can finalize reports.")
a=ap.parse_args()
nift=str(pathlib.Path(a.nift).resolve())
def git_commit():
    try: return subprocess.check_output(["git","rev-parse","HEAD"],cwd=pathlib.Path(__file__).resolve().parents[1],text=True,stderr=subprocess.DEVNULL).strip()
    except Exception: return "unknown"

def rss_kib(pid):
    try:
        for line in pathlib.Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"): return int(line.split()[1])
    except FileNotFoundError: return None
    return None
with tempfile.TemporaryDirectory(prefix="nift-cp4-watch-") as td:
    root=pathlib.Path(td)
    subprocess.run([nift,"init",".html"],cwd=root,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
    # Add project contract + schema-validated JSON so watch repeatedly owns those paths.
    cfg=json.loads((root/".nift/config.json").read_text())
    cfg["config"]["contracts"]={"routes":".nift/routes.json"}
    (root/".nift/config.json").write_text(json.dumps(cfg,separators=(",",":")))
    (root/".nift/routes.json").write_text('{"home":"/"}\n')
    (root/"data").mkdir(); (root/"schemas").mkdir()
    (root/"data/state.json").write_text('{"name":"ok","n":0}\n')
    (root/"schemas/state.schema.json").write_text('{"type":"object","required":["name","n"],"properties":{"name":{"type":"string"},"n":{"type":"integer"}}}\n')
    (root/"templates/template.html").write_text(
        '@json("data/state.json", state, "schemas/state.schema.json")\n'
        '<a href="$[routes.home]">$[state.name]-$[state.n]</a>\n@content\n')
    subprocess.run([nift,"build-all"],cwd=root,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
    # Use a dedicated process group/session. This matters when --nift is a
    # supervisor such as valgrind_nift.sh: build-auto runs underneath Valgrind,
    # and signalling only the supervisor PID can leave the monitored process
    # alive while Valgrind waits forever for it to exit.
    p=subprocess.Popen([nift,"build-auto"],cwd=root,stdout=subprocess.DEVNULL,
                       stderr=subprocess.PIPE,text=True,start_new_session=True)
    samples=[]; started=time.monotonic()
    try:
        time.sleep(.8)
        for i in range(a.cycles):
            phase=i%5
            if phase==0:
                (root/"content/index.html").write_text(f"<p>content-{i}</p>\n")
            elif phase==1:
                (root/".nift/routes.json").write_text(json.dumps({"home":f"/r/{i}"})+"\n")
            elif phase==2:
                (root/"data/state.json").write_text(json.dumps({"name":"ok","n":i})+"\n")
            elif phase==3:
                (root/"templates/template.html").write_text(
                    '@json("data/state.json", state, "schemas/state.schema.json")\n'
                    f'<main data-cycle="{i}"><a href="$[routes.home]">$[state.name]-$[state.n]</a></main>\n@content\n')
            else:
                (root/"data/state.json").write_text(json.dumps({"name":"rotated","n":i})+"\n")
            time.sleep(a.interval)
            if p.poll() is not None:
                raise RuntimeError(f"build-auto exited early with {p.returncode}: {p.stderr.read()}")
            if i>=20 and (i%20==0 or i==a.cycles-1):
                samples.append({"cycle":i,"rss_kib":rss_kib(p.pid)})
    finally:
        shutdown="already-exited"
        if p.poll() is None:
            shutdown="sigint"
            try:
                os.killpg(p.pid, signal.SIGINT)
            except ProcessLookupError:
                pass
            try:
                p.wait(timeout=a.shutdown_grace_seconds)
            except subprocess.TimeoutExpired:
                shutdown="sigterm"
                try:
                    os.killpg(p.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    p.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    shutdown="sigkill"
                    try:
                        os.killpg(p.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    p.wait(timeout=5)
    vals=[x["rss_kib"] for x in samples if x["rss_kib"] is not None]
    first=vals[0] if vals else None; mid=vals[len(vals)//2] if vals else None; last=vals[-1] if vals else None
    data={"schema_version":1,"checkpoint":"4-watch","commit":git_commit(),"platform":platform.platform(),"cycles":a.cycles,"interval_seconds":a.interval,
          "elapsed_seconds":round(time.monotonic()-started,3),"rss_samples":samples,
          "warm_sample_kib":first,"mid_sample_kib":mid,"final_sample_kib":last,
          "max_sample_kib":max(vals) if vals else None,
          "process_exit_status":p.returncode,"shutdown":shutdown,"pass":p.returncode in (-2,130,0) and shutdown in ("already-exited","sigint") and bool(vals)}
    out=pathlib.Path(a.output)
    if not out.is_absolute(): out=pathlib.Path.cwd()/out
    out.parent.mkdir(parents=True,exist_ok=True); out.write_text(json.dumps(data,indent=2)+"\n")
    print("checkpoint 4 watch endurance:", "PASS" if data["pass"] else "FAIL")
    print(f"cycles={a.cycles} elapsed={data['elapsed_seconds']}s rss={first}->{mid}->{last} KiB max={data['max_sample_kib']}")
    print(f"evidence={out}")
    raise SystemExit(0 if data["pass"] else 1)
