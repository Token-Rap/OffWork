"""Calendar popup smoke (review finding 1).

Opening the resignation calendar used to build a WebView2 window inside a
synchronous command, which deadlocks the Windows event loop. This drives the
real path (F4 handler -> open_calendar -> popup -> calendar_init ->
calendar_finish) and then proves the main loop still answers by hiding the
widget. A deadlock shows up as a timeout at the first missing marker.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding="utf-8")
binary = Path(sys.argv[1]).resolve()


def wait(path: Path, app: subprocess.Popen, seconds: float = 90) -> float:
    start = time.monotonic()
    while not path.exists():
        if app.poll() is not None:
            raise RuntimeError(f"app exited before {path.name}")
        if time.monotonic() - start > seconds:
            raise RuntimeError(f"timed out waiting for {path.name} (deadlock?)")
        time.sleep(0.05)
    return round(time.monotonic() - start, 2)


with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    measure = root / "measure"
    measure.mkdir()
    env = {**os.environ, "OFFWORK_MEASURE": str(measure), "OFFWORK_PROFILE_DIR": str(root / "profile"),
           "HOME": str(root), "XDG_CONFIG_HOME": str(root / ".config")}
    app = subprocess.Popen([str(binary)], env=env)
    try:
        timings = {"frame": wait(measure / "frame", app, 120)}
        (measure / "open-calendar").touch()
        timings["calendar_opened"] = wait(measure / "calendar-opened", app)
        (measure / "cancel-calendar").touch()
        timings["calendar_closed"] = wait(measure / "calendar-closed", app)
        (measure / "hide").touch()
        timings["main_loop_answers"] = wait(measure / "hidden", app)
        print("calendar popup opened, cancelled, main loop responsive:", timings)
    finally:
        app.terminate()
        app.wait(timeout=15)
