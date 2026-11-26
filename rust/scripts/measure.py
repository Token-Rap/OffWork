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
    processes = {p.pid:p for p in [root, *root.children(recursive=True)]}
    if sys.platform == "darwin":
        group = coalition(root.pid)
        for p in psutil.process_iter(["name"]):
            if group and "WebKit" in (p.info["name"] or "") and coalition(p.pid) == group:
                processes[p.pid] = p
    rss = 0
    cpu = {}
    for pid, proc in processes.items():
        try:
            rss += proc.memory_info().rss
            times = proc.cpu_times()
            cpu[pid] = times.user + times.system
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return rss, cpu

def wait_until(condition, process, label):
    deadline = time.monotonic() + 45
    while not condition():
        if process.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError(f"Timed out waiting for {label}, process status {process.poll()}")
        time.sleep(.005)

parser = argparse.ArgumentParser()
parser.add_argument("binary",type=Path)
parser.add_argument("output",type=Path)
parser.add_argument("--legacy-windows",action="store_true")
parser.add_argument("--seconds",type=int,default=600)
args = parser.parse_args()
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
        result = {("launch_to_visible_window_ms" if args.legacy_windows else "launch_to_second_raf_ms"):round((time.perf_counter()-started)*1000), "seconds_per_state":args.seconds}
        root = psutil.Process(proc.pid)
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
            _, previous = sample(root)
            usage = 0
            sizes = []
            pids = set(previous)
            while time.perf_counter()-start < args.seconds:
                time.sleep(min(5,max(0,args.seconds-(time.perf_counter()-start))))
                if proc.poll() is not None: raise RuntimeError(f"App exited during {state} sampling")
                rss,counts = sample(root)
                usage += sum(max(0,count-previous.get(pid,0)) for pid,count in counts.items())
                previous = counts
                pids.update(counts)
                sizes.append(rss/1048576)
            elapsed = time.perf_counter()-start
            result[state] = {"avg_rss_mib":round(statistics.mean(sizes),2),"peak_rss_mib":round(max(sizes),2),"cpu_pct_one_core":round(100*usage/elapsed,3),"pids":sorted(pids)}
            print(state,result[state],flush=True)
        args.output.write_text(json.dumps(result,indent=2)+"\n")
    finally:
        proc.terminate()
        proc.wait(timeout=15)
