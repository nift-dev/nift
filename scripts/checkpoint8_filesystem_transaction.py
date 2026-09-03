#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, os, pathlib, platform, shutil, signal, subprocess, tempfile, time
try:
    import resource
except ImportError:
    resource=None

ap=argparse.ArgumentParser()
ap.add_argument("--nift",required=True)
ap.add_argument("--output",required=True)
a=ap.parse_args(); NIFT=str(pathlib.Path(a.nift).resolve()); repo=pathlib.Path(__file__).resolve().parents[1]

def commit():
    try:return subprocess.check_output(["git","rev-parse","HEAD"],cwd=repo,text=True,stderr=subprocess.DEVNULL).strip()
    except Exception:return "unknown"
def run(root,*args,ok=True,timeout=15):
    p=subprocess.run([NIFT,*args],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout)
    if ok and p.returncode:
        raise RuntimeError(f"{args} failed: {p.stdout}\n{p.stderr}")
    if not ok and p.returncode==0:
        raise RuntimeError(f"{args} unexpectedly succeeded")
    return p
def scaffold(root):
    run(root,"init")
    # Keep one deterministic page.
    tr=json.loads((root/".nift/tracked.json").read_text())
    tr["tracked"]=[{"name":"/","title":"Home","template":"templates/template.html"}]
    (root/".nift/tracked.json").write_text(json.dumps(tr,separators=(",",":"))+"\n")
    (root/"templates/template.html").write_text("<main>@content</main>\n")
    (root/"content/index.html").write_text("<p>BASE</p>\n")
    run(root,"build", "--all")
def preserve_pair(root):
    return (root/"public/index.html").read_bytes(), (root/".nift/public/index.info.json").read_bytes()
def assert_pair(root,pair,label):
    if (root/"public/index.html").read_bytes()!=pair[0]: raise RuntimeError(label+": prior output changed")
    if (root/".nift/public/index.info.json").read_bytes()!=pair[1]: raise RuntimeError(label+": prior metadata changed")
def chmod_restore(path,mode=0o644):
    try: os.chmod(path,mode)
    except FileNotFoundError: pass

started=time.monotonic(); results=[]
def case(name,fn):
    t=time.monotonic()
    try:
        detail=fn() or {}
        results.append({"case":name,"pass":True,"elapsed_seconds":round(time.monotonic()-t,6),**detail})
    except Exception as e:
        results.append({"case":name,"pass":False,"error":str(e)})
        raise

with tempfile.TemporaryDirectory(prefix="nift-cp8-") as td:
    td=pathlib.Path(td)

    def unreadable_content():
        root=td/"unread-content"; root.mkdir(); scaffold(root); prior=preserve_pair(root)
        p=root/"content/index.html"; p.write_text("<p>NEW</p>\n"); os.chmod(p,0)
        try:
            r=run(root,"build", "--all",ok=False)
            if "not readable" not in (r.stdout+r.stderr): raise RuntimeError("missing readable-source diagnostic")
            assert_pair(root,prior,"unreadable content")
        finally: chmod_restore(p)
        return {"diagnostic":"content file is not readable"}
    case("unreadable-content-preserves-last-good",unreadable_content)

    def unreadable_template():
        root=td/"unread-template"; root.mkdir(); scaffold(root); prior=preserve_pair(root)
        p=root/"templates/template.html"; p.write_text("<section>@content</section>\n"); os.chmod(p,0)
        try:
            r=run(root,"build", "--all",ok=False)
            if "not readable" not in (r.stdout+r.stderr): raise RuntimeError("missing template-readable diagnostic")
            assert_pair(root,prior,"unreadable template")
        finally: chmod_restore(p)
        return {}
    case("unreadable-template-preserves-last-good",unreadable_template)

    def unreadable_input():
        root=td/"unread-input"; root.mkdir(); scaffold(root); prior=preserve_pair(root)
        p=root/"templates/head.html"; p.write_text('<meta charset="utf-8">\n')
        (root/"templates/template.html").write_text('<head>@input("templates/head.html")</head><main>@content</main>\n')
        run(root,"build", "--all"); prior=preserve_pair(root)
        p.write_text('<link>new</link>\n'); os.chmod(p,0)
        try:
            r=run(root,"build", "--all",ok=False)
            if "not readable" not in (r.stdout+r.stderr): raise RuntimeError("missing input-readable diagnostic")
            assert_pair(root,prior,"unreadable input")
        finally: chmod_restore(p)
        return {"diagnostic":"input file is not readable"}
    case("unreadable-input-preserves-last-good",unreadable_input)

    def unreadable_json():
        root=td/"unread-json"; root.mkdir(); scaffold(root)
        (root/"data").mkdir(); j=root/"data/site.json"; j.write_text('{"name":"ok"}\n')
        (root/"templates/template.html").write_text('@json("data/site.json", site)\n<b>$[site.name]</b>@content\n')
        run(root,"build", "--all"); prior=preserve_pair(root)
        j.write_text('{"name":"new"}\n'); os.chmod(j,0)
        try:
            r=run(root,"build", "--all",ok=False)
            if "not readable" not in (r.stdout+r.stderr): raise RuntimeError("missing JSON-readable diagnostic")
            assert_pair(root,prior,"unreadable JSON")
        finally: chmod_restore(j)
        return {}
    case("unreadable-json-preserves-last-good",unreadable_json)

    def dangling_symlink():
        root=td/"dangling"; root.mkdir(); scaffold(root); prior=preserve_pair(root)
        link=root/"templates/missing-link.html"; link.symlink_to(root/"templates/does-not-exist.html")
        (root/"templates/template.html").write_text('@input("templates/missing-link.html")\n@content\n')
        r=run(root,"build", "--all",ok=False)
        if "does not exist" not in (r.stdout+r.stderr): raise RuntimeError("dangling symlink did not fail clearly")
        assert_pair(root,prior,"dangling symlink")
        return {}
    case("dangling-symlink-fails-safely",dangling_symlink)

    def symlink_loop():
        root=td/"loop"; root.mkdir(); scaffold(root); prior=preserve_pair(root)
        a1=root/"templates/a.html"; b1=root/"templates/b.html"
        a1.symlink_to(b1); b1.symlink_to(a1)
        (root/"templates/template.html").write_text('@input("templates/a.html")\n@content\n')
        r=run(root,"build", "--all",ok=False,timeout=5)
        assert_pair(root,prior,"symlink loop")
        return {"exit":r.returncode}
    case("symlink-loop-does-not-hang",symlink_loop)

    def json_replaced_by_directory():
        root=td/"json-dir"; root.mkdir(); scaffold(root)
        (root/"data").mkdir(); p=root/"data/site.json"; p.write_text('{"v":1}\n')
        (root/"templates/template.html").write_text('@json("data/site.json", site)\n$[site.v]@content\n')
        run(root,"build", "--all"); prior=preserve_pair(root)
        p.unlink(); p.mkdir()
        r=run(root,"build", "--all",ok=False)
        if r.returncode < 0 or "terminate called" in (r.stdout+r.stderr):
            raise RuntimeError("directory replacement caused process crash")
        assert_pair(root,prior,"JSON replaced by directory")
        return {"exit":r.returncode,"diagnostic_tail":"\n".join((r.stdout+r.stderr).splitlines()[-3:])}
    case("file-replaced-by-directory-fails-safely",json_replaced_by_directory)

    def output_parent_readonly():
        root=td/"readonly-output"; root.mkdir(); scaffold(root)
        prior=preserve_pair(root)
        # Remove generated output, make output directory non-writable, force rebuild.
        (root/"public/index.html").unlink()
        os.chmod(root/"public",0o555)
        try:
            r=run(root,"build", "--all",ok=False)
            if "failed to write generated output" not in (r.stdout+r.stderr): raise RuntimeError("missing output-write failure")
            # Metadata must not be refreshed to claim success.
            if (root/".nift/public/index.info.json").read_bytes()!=prior[1]: raise RuntimeError("metadata changed after output write failure")
        finally: os.chmod(root/"public",0o755)
        return {}
    case("readonly-output-directory-does-not-claim-success",output_parent_readonly)

    def metadata_write_failure():
        root=td/"metadata-failure"; root.mkdir(); scaffold(root); prior=preserve_pair(root)
        (root/"content/index.html").write_text("<p>NEW-META</p>\n")
        info=root/".nift/public/index.info.json"; info.unlink(); info.mkdir()
        r=run(root,"build", "--all",ok=False)
        if "failed to write page build metadata" not in (r.stdout+r.stderr): raise RuntimeError("missing metadata-write diagnostic")
        if b"NEW-META" not in (root/"public/index.html").read_bytes(): raise RuntimeError("rendered output did not finish before metadata obstruction")
        info.rmdir()
        # With metadata absent, status must say rebuild rather than silently clean.
        s=run(root,"status")
        if "needs rebuilding" not in s.stdout: raise RuntimeError("missing metadata did not leave page stale")
        # The failed build left a durable .unfinished marker (v4.0.7+): a plain
        # build refuses and repair is the only mode that reconstructs the
        # incomplete derived state.
        run(root,"build", "--repair")
        if b"NEW-META" not in (root/"public/index.html").read_bytes():
            raise RuntimeError("repair did not rebuild the page after metadata obstruction")
        return {}
    case("metadata-write-failure-remains-stale-and-repairs",metadata_write_failure)

    def unicode_long_path():
        root=td/"unicode-long"; root.mkdir(); scaffold(root)
        name="深い/δοκιμή/" + "/".join(("segment-"*5)+str(i) for i in range(8)) + "/ページ"
        p=root/"content"/(name+".html"); p.parent.mkdir(parents=True); p.write_text("<p>ユニコード ✓</p>\n")
        r=run(root,"track",name,"Unicode long path","templates/template.html")
        run(root,"build")
        out=root/"public"/(name+".html")
        if not out.exists() or "ユニコード" not in out.read_text(): raise RuntimeError("unicode/long output missing")
        return {"name_length":len(name)}
    case("unicode-and-long-relative-path",unicode_long_path)


    def output_directory_replaced_by_file():
        root=td/"out-parent-file"; root.mkdir(); scaffold(root)
        # Add a nested tracked page and establish valid output/metadata.
        p=root/"content/sub/page.html"; p.parent.mkdir(parents=True); p.write_text("<p>NESTED</p>\n")
        run(root,"track","sub/page","Nested","templates/template.html")
        run(root,"build")
        info=(root/".nift/public/sub/page.info.json").read_bytes()
        shutil.rmtree(root/"public/sub")
        (root/"public/sub").write_text("not-a-directory\n")
        (root/"content/sub/page.html").write_text("<p>CHANGED</p>\n")
        r=run(root,"build", "--all",ok=False)
        if "failed to write generated output" not in (r.stdout+r.stderr):
            raise RuntimeError("file replacing output directory did not produce controlled write failure")
        if (root/".nift/public/sub/page.info.json").read_bytes()!=info:
            raise RuntimeError("nested metadata changed after output-parent obstruction")
        return {}
    case("output-directory-replaced-by-file-fails-safely",output_directory_replaced_by_file)

    def readonly_project_state_directory():
        root=td/"readonly-state"; root.mkdir(); scaffold(root)
        before=(root/".nift/tracked.json").read_bytes()
        os.chmod(root/".nift",0o555)
        try:
            r=run(root,"track","blocked","Blocked","templates/template.html",ok=False)
            if r.returncode==0: raise RuntimeError("track succeeded with read-only state directory")
            if (root/".nift/tracked.json").read_bytes()!=before:
                raise RuntimeError("tracked state changed despite failed state write")
        finally: os.chmod(root/".nift",0o755)
        # Any orphan content is incomplete work, but state remains authoritative
        # and a subsequent normal command must continue to work.
        run(root,"track","recovered","Recovered","templates/template.html")
        return {}
    case("readonly-state-directory-preserves-tracking",readonly_project_state_directory)


    def partial_direct_write_marks_unfinished_and_repairs():
        if resource is None:
            return {"skipped":"RLIMIT_FSIZE unavailable"}
        root=td/"write-limit"; root.mkdir(); scaffold(root)
        # Establish a complete multi-megabyte output first (direct write).
        content=root/"content/index.html"; content.write_text("B"*(4*1024*1024))
        (root/"templates/template.html").write_text("@content")
        run(root,"build", "--all",timeout=20)
        # Force a different multi-megabyte result so identical-output elision
        # cannot bypass the write-pressure path under test. CP3 direct writes
        # are in-place truncate (no temp, no atomic replace): a failed write may
        # leave a truncated output, and the durable marker + --repair are the
        # documented recovery contract.
        content.write_text("C"*(4*1024*1024))
        def limit_file_size():
            resource.setrlimit(resource.RLIMIT_FSIZE,(1024*1024,1024*1024))
            signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
        p=subprocess.run([NIFT,"build", "--all"],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                         preexec_fn=limit_file_size,timeout=20)
        if p.returncode==0: raise RuntimeError("file-size-limited build unexpectedly claimed success")
        marker=root/".nift/.unfinished"
        if not marker.exists(): raise RuntimeError("direct-write failure did not leave .unfinished marker")
        r=run(root,"build", "--all",ok=False)
        if "unfinished build detected" not in (r.stdout+r.stderr):
            raise RuntimeError("plain build did not refuse while the marker remains")
        run(root,"build", "--repair",timeout=20)
        if (root/"public/index.html").stat().st_size != (4*1024*1024):
            raise RuntimeError("repair did not reconstruct the complete output")
        if (root/"public/index.html").read_bytes() != b"C"*(4*1024*1024):
            raise RuntimeError("repaired output content is wrong")
        if marker.exists(): raise RuntimeError("repair did not clear .unfinished marker")
        s=run(root,"status")
        if "up to date" not in s.stdout: raise RuntimeError("status not clean after repair")
        return {"exit":p.returncode,"limit_bytes":1024*1024}
    case("partial-direct-write-marks-unfinished-and-repairs",partial_direct_write_marks_unfinished_and_repairs)

    def stale_temp_recovery_is_bounded_and_concurrency_safe():
        root=td/"stale-temp-recovery"; root.mkdir(); scaffold(root)
        public=root/"public"
        # A dead-owner temporary left by an interrupted process must be removed.
        dead_pid=99999999
        stale=public/f"unrelated.html.nift-tmp-{dead_pid}-17"
        stale.write_text("stale\n")
        # A temporary owned by this still-live Python process models another
        # concurrent writer and must not be removed by Nift recovery.
        live=public/f"concurrent.html.nift-tmp-{os.getpid()}-23"
        live.write_text("live\n")
        (root/"content/index.html").write_text("<p>RECOVER</p>\n")
        run(root,"build", "--all")
        if stale.exists(): raise RuntimeError("dead-owner stale temporary survived recovery")
        if not live.exists(): raise RuntimeError("live-owner concurrent temporary was removed")
        live.unlink()
        return {"dead_owner_removed":True,"live_owner_preserved":True}
    case("stale-temp-recovery-is-bounded-and-concurrency-safe",stale_temp_recovery_is_bounded_and_concurrency_safe)

    def build_auto_recovers_mid_session_stale_temp_on_relevant_activity():
        root=td/"build-auto-stale-recovery"; root.mkdir(); scaffold(root)
        public=root/"public"
        # Make the first build --auto pass perform a real output write. This is
        # essential to the regression shape: the long-running process must have
        # already scanned public/ before the stale temp is planted.
        content=root/"content/index.html"
        content.write_text("<p>BUILD-AUTO-FIRST-PASS</p>\n")
        process=subprocess.Popen([NIFT,"build", "--auto"],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            deadline=time.monotonic()+8
            output=root/"public/index.html"
            while time.monotonic()<deadline and process.poll() is None:
                if output.exists() and "BUILD-AUTO-FIRST-PASS" in output.read_text(): break
                time.sleep(.05)
            else:
                if process.poll() is not None: raise RuntimeError("build --auto exited before initial write pass")
                raise RuntimeError("build --auto did not complete initial write pass")

            stale=public/"mid-session.html.nift-tmp-99999999-31"
            stale.write_text("stale\n")

            # No background garbage collection is promised: an idle poll with no
            # relevant output write must not be required to remove this artifact.
            time.sleep(.35)
            if not stale.exists():
                raise RuntimeError("idle build --auto unexpectedly performed background stale-temp collection")

            # Relevant activity starts a new recovery epoch. The first write to
            # public/ in that pass must recover the dead-owner temp without a
            # process restart.
            content.write_text("<p>MID-SESSION-RECOVERY</p>\n")
            deadline=time.monotonic()+8
            while time.monotonic()<deadline and process.poll() is None:
                output=root/"public/index.html"
                if output.exists() and "MID-SESSION-RECOVERY" in output.read_text() and not stale.exists():
                    return {"idle_retained":True,"next_relevant_pass_removed":True}
                time.sleep(.05)
            if process.poll() is not None: raise RuntimeError("build --auto exited during recovery pass")
            if stale.exists(): raise RuntimeError("mid-session stale temporary survived subsequent relevant build activity")
            raise RuntimeError("build --auto did not publish changed output during recovery pass")
        finally:
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(timeout=5)
    case("build-auto-recovers-mid-session-stale-temp-on-relevant-activity",build_auto_recovers_mid_session_stale_temp_on_relevant_activity)

    def identical_output_rebuild_refreshes_current_state():
        root=td/"identical-output-refresh"; root.mkdir(); scaffold(root)
        (root/"data").mkdir(); marker=root/"data/marker.txt"; marker.write_text("A\n")
        (root/"templates/template.html").write_text('@dep("data/marker.txt")<main>@content</main>\n')
        run(root,"build", "--all")
        before=(root/"public/index.html").read_bytes()
        # Change a declared dependency without changing rendered output. The
        # optimized writer may avoid replacing identical bytes, but page-info
        # mtime must still advance so modified-mode status becomes clean.
        time.sleep(.02); marker.write_text("B\n")
        run(root,"build", "--all")
        if (root/"public/index.html").read_bytes()!=before:
            raise RuntimeError("non-rendered dependency change altered output")
        status=run(root,"status")
        if "up to date" not in status.stdout:
            raise RuntimeError("identical-output rebuild did not refresh current-state metadata")
        return {"rendered_bytes_unchanged":True,"status_clean":True}
    case("identical-output-rebuild-refreshes-current-state",identical_output_rebuild_refreshes_current_state)

    def sigkill_during_direct_write_marks_unfinished_and_repairs():
        root=td/"kill-write"; root.mkdir(); scaffold(root)
        size=48*1024*1024
        content=root/"content/index.html"; content.write_text("A"*size)
        (root/"templates/template.html").write_text("@content")
        run(root,"build", "--all",timeout=30)
        # Force a different result so identical-output elision cannot bypass the
        # direct-write rewrite being interrupted.
        content.write_text("B"*size)
        # CP3 direct-write: no output temp file exists. Kill while the durable
        # marker is present and the in-place output has started growing (the
        # multi-MB write is then mid-flight).
        p=subprocess.Popen([NIFT,"build", "--all"],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        deadline=time.monotonic()+10; seen=False
        while time.monotonic()<deadline and p.poll() is None:
            if (root/".nift/.unfinished").exists() and (root/"public/index.html").exists():
                sz=(root/"public/index.html").stat().st_size
                if 0 < sz < size:
                    seen=True; break
            time.sleep(.001)
        if not seen:
            p.kill(); p.wait(); raise RuntimeError("did not observe mid-flight direct write")
        os.kill(p.pid,signal.SIGKILL); p.wait(timeout=5)
        marker=root/".nift/.unfinished"
        if not marker.exists(): raise RuntimeError("killed direct write did not leave .unfinished marker")
        r=run(root,"build", "--all",ok=False)
        if "unfinished build detected" not in (r.stdout+r.stderr):
            raise RuntimeError("plain build did not refuse while the marker remains")
        run(root,"build", "--repair",timeout=30)
        if (root/"public/index.html").stat().st_size != size:
            raise RuntimeError("repair did not reconstruct the complete output")
        if (root/"public/index.html").read_bytes() != b"B"*size:
            raise RuntimeError("repaired output content is wrong")
        if marker.exists(): raise RuntimeError("repair did not clear .unfinished marker")
        s=run(root,"status")
        if "up to date" not in s.stdout: raise RuntimeError("status not clean after repair")
        return {"kill_exit":p.returncode,"bytes":size}
    case("sigkill-during-direct-write-marks-unfinished-and-repairs",sigkill_during_direct_write_marks_unfinished_and_repairs)

out=pathlib.Path(a.output); out.parent.mkdir(parents=True,exist_ok=True)
data={"schema_version":1,"checkpoint":"8-filesystem-transaction","commit":commit(),"platform":platform.platform(),
      "cases":results,"case_count":len(results),"elapsed_seconds":round(time.monotonic()-started,3),
      "pass":all(x["pass"] for x in results)}
out.write_text(json.dumps(data,indent=2)+"\n")
print(f"checkpoint 8 filesystem/transaction: {'PASS' if data['pass'] else 'FAIL'} ({len(results)} cases)")
print("evidence="+str(out))
raise SystemExit(0 if data["pass"] else 1)
