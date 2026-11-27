"""Launch a release build and sample private benchmark state and process-tree RSS."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time

import psutil

def coalition(pid):
    # Darwin's resource coalition includes out-of-process WebKit XPC services.
    ids = (ctypes.c_uint64 * 5)()
    copied = libproc.proc_pidinfo(pid, 20, 0, ids, ctypes.sizeof(ids))
    return ids[0] if copied == ctypes.sizeof(ids) else None

if sys.platform == "darwin":
    libproc = ctypes.CDLL("/usr/lib/libproc.dylib")

def sample(root):
    """Process-tree working set (RSS), private bytes (Windows) and CPU times."""
    processes = {p.pid:p for p in [root, *root.children(recursive=True)]}
    if sys.platform == "darwin":
        group = coalition(root.pid)
        for p in psutil.process_iter(["name"]):
            if group and "WebKit" in (p.info["name"] or "") and coalition(p.pid) == group:
                processes[p.pid] = p
    rss = 0
    private = 0
    cpu = {}
    for pid, proc in processes.items():
        try:
            info = proc.memory_info()
            rss += info.rss
            private += getattr(info, "private", 0)
            times = proc.cpu_times()
            cpu[pid] = times.user + times.system
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return rss, private, cpu

def wait_until(condition, process, label):
    deadline = time.monotonic() + 120  # shared runners can stall WebView2 cold start
    while not condition():
        if process.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError(f"Timed out waiting for {label}, process status {process.poll()}")
        time.sleep(.005)

parser = argparse.ArgumentParser()
parser.add_argument("binary",type=Path)
parser.add_argument("output",type=Path)
parser.add_argument("--legacy-windows",action="store_true")
parser.add_argument("--seconds",type=int,default=600,help="seconds per state unless overridden")
parser.add_argument("--visible-seconds",type=int)
parser.add_argument("--hidden-seconds",type=int)
parser.add_argument("--max-hidden-growth-mib",type=float,
    help="fail when the hidden working set's last-minute median exceeds its first-minute median by more")
args = parser.parse_args()
durations = {"visible": args.visible_seconds or args.seconds, "hidden": args.hidden_seconds or args.seconds}
MIB = 1048576
with tempfile.TemporaryDirectory() as temp:
    folder = Path(temp)
    started = time.perf_counter()
    proc = subprocess.Popen([str(args.binary.resolve())],env={**os.environ,"OFFWORK_MEASURE":temp})
    try:
        if args.legacy_windows:
            user32=ctypes.windll.user32
            user32.FindWindowW.restype=ctypes.c_void_p
            def legacy_visible():
                handle=user32.FindWindowW("OffWork.Native.Widget",None)
                if not handle: return False
                owner=ctypes.c_ulong()
                user32.GetWindowThreadProcessId(ctypes.c_void_p(handle),ctypes.byref(owner))
                return owner.value==proc.pid and user32.IsWindowVisible(ctypes.c_void_p(handle))
            wait_until(legacy_visible,proc,"legacy window")
        else:
            wait_until((folder/"frame").exists,proc,"first frame")
        result = {("launch_to_visible_window_ms" if args.legacy_windows else "launch_to_second_raf_ms"):round((time.perf_counter()-started)*1000), "seconds_per_state":durations}
        root = psutil.Process(proc.pid)
        failures = []
        for state in ("visible","hidden"):
            if state == "hidden":
                if args.legacy_windows:
                    handle=user32.FindWindowW("OffWork.Native.Widget",None)
                    user32.PostMessageW(ctypes.c_void_p(handle),0x10,0,0)
                    wait_until(lambda:not legacy_visible(),proc,"legacy window hidden")
                else:
                    (folder/"hide").touch()
                    wait_until((folder/"hidden").exists,proc,"hidden window")
            start = time.perf_counter()
            _, _, previous = sample(root)
            usage = 0
            series = []
            pids = set(previous)
            while time.perf_counter()-start < durations[state]:
                time.sleep(min(5,max(0,durations[state]-(time.perf_counter()-start))))
                if proc.poll() is not None: raise RuntimeError(f"App exited during {state} sampling")
                rss,private,counts = sample(root)
                usage += sum(max(0,count-previous.get(pid,0)) for pid,count in counts.items())
                previous = counts
                pids.update(counts)
                series.append((round(time.perf_counter()-start,1), round(rss/MIB,2), round(private/MIB,2)))
            elapsed = time.perf_counter()-start
            sizes = [s[1] for s in series]
            minute = max(1, round(60/5))
            first, last = statistics.median(sizes[:minute]), statistics.median(sizes[-minute:])
            summary = {"avg_rss_mib":round(statistics.mean(sizes),2),"peak_rss_mib":round(max(sizes),2),
                "first_minute_median_mib":round(first,2),"last_minute_median_mib":round(last,2),
                "growth_mib":round(last-first,2),"cpu_pct_one_core":round(100*usage/elapsed,3),"pids":sorted(pids)}
            if sys.platform == "win32":
                summary["avg_private_mib"] = round(statistics.mean(s[2] for s in series),2)
            print(state,summary,flush=True)
            result[state] = {**summary,"series_s_rss_private":series}
            if state == "hidden" and args.max_hidden_growth_mib is not None and last-first > args.max_hidden_growth_mib:
                failures.append(f"hidden working set grew {last-first:.1f} MiB over {elapsed:.0f} s (limit {args.max_hidden_growth_mib} MiB)")
        args.output.write_text(json.dumps(result,indent=2)+"\n")
        if failures:
            raise SystemExit("; ".join(failures))
    finally:
        proc.terminate()
        proc.wait(timeout=15)
