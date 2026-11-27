"""Hidden-window reminder smoke: the five-minute reminder fires once, on time,
while the widget is hidden, and a fresh launch inside the window stays silent.

Settings live in a temporary profile (OFFWORK_PROFILE_DIR),
with a fixed end time five minutes after the next whole minute.
"""
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding="utf-8")  # Windows consoles default to cp1252
binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    measure = root / "measure"
    measure.mkdir()
    profile = root / "profile"
    env = {**os.environ, "OFFWORK_MEASURE": str(measure), "OFFWORK_PROFILE_DIR": str(profile),
           "HOME": str(root), "XDG_CONFIG_HOME": str(root / ".config")}
    settings = profile / "settings.json"
    now = dt.datetime.now()
    if now.second > 45:  # leave room for the cold start
        time.sleep(61 - now.second)
        now = dt.datetime.now()
    boundary = (now + dt.timedelta(minutes=1)).replace(second=0, microsecond=0)
    minute = boundary.hour * 60 + boundary.minute
    settings.parent.mkdir(parents=True)
    settings.write_text(json.dumps({
        "schemaVersion": 1,
        "work": {"startMinutes": (minute - 120) % 1440, "endMinutes": (minute + 5) % 1440,
                 "endInputMode": "endTime", "lunchStartMinutes": -1, "lunchEndMinutes": -1},
    }))
    app = subprocess.Popen([str(binary)], env=env)
    try:
        deadline = time.monotonic() + 120
        while not (measure / "frame").exists():
            if app.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("widget did not render")
            time.sleep(0.05)
        (measure / "hide").touch()
        while not (measure / "hidden").exists():
            time.sleep(0.05)
        wait = (boundary - dt.datetime.now()).total_seconds() + 4
        time.sleep(max(0, wait))
        log = (measure / "notifications.log").read_text(encoding="utf-8") if (measure / "notifications.log").exists() else ""
        print("expected at", boundary.strftime("%H:%M:%S"), "log:", log.strip())
        lines = log.splitlines()
        assert len(lines) == 1 and lines[0].endswith("还有五分钟下班"), lines
        fired = dt.datetime.combine(boundary.date(), dt.time.fromisoformat(lines[0].split()[0]))
        assert abs((fired - boundary).total_seconds()) <= 2, (fired, boundary)
    finally:
        app.terminate()
        app.wait(timeout=15)
    # Relaunch inside the five-minute window: startup never catches up.
    (measure / "notifications.log").unlink()
    for name in ("frame", "hide", "hidden"):
        (measure / name).unlink(missing_ok=True)
    app = subprocess.Popen([str(binary)], env=env)
    try:
        time.sleep(8)
        assert not (measure / "notifications.log").exists(), "startup inside the window must stay silent"
    finally:
        app.terminate()
        app.wait(timeout=15)
    print("hidden five-minute reminder fired once on time; relaunch stayed silent")
