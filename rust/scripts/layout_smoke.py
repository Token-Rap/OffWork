"""Expanded-layout smoke at 75/100/125/150 % (real-machine report: at
1920x1080 the panel was cut off at 125 % and the window overflowed the work
area by 132 px at 150 %).

For each scale the release app starts with that scale saved, expands its
settings, scrolls to the end and reports (OFFWORK_MEASURE hook): the window
must lie inside the monitor work area and the last control must be inside
the window. Usage: layout_smoke.py <binary> <label>; writes
artifacts/layout-<label>.json.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding="utf-8")
binary, label = Path(sys.argv[1]).resolve(), sys.argv[2]
TOLERANCE = 1.5  # sub-pixel rounding between CSS and device pixels


def wait(path: Path, app: subprocess.Popen, seconds: float = 120) -> None:
    deadline = time.monotonic() + seconds
    while not path.exists():
        if app.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError(f"no {path.name} (exit {app.poll()})")
        time.sleep(0.05)


def check(report: dict) -> list[str]:
    problems = []
    win, work, page = report["window"], report["work_area"], report["page"]
    if win["x"] < work["x"] - 1 or win["x"] + win["w"] > work["x"] + work["w"] + 1:
        problems.append(f"window x {win} outside work area {work}")
    if win["y"] < work["y"] - 1 or win["y"] + win["h"] > work["y"] + work["h"] + 1:
        problems.append(f"window y {win} outside work area {work}")
    height = page["viewport"]["height"]
    for name in ("main", "lastControl", "status"):
        r = page[name]
        if r is None:
            problems.append(f"{name} missing")
        elif r["top"] < -TOLERANCE or r["bottom"] > height + TOLERANCE:
            problems.append(f"{name} {r} not inside the {height}px window after scrolling")
    for name, seen in page["visible"].items():
        if not seen:
            problems.append(f"{name} is clipped (hit test at its centre finds another element)")
    if page["controls"] < 10:
        problems.append(f"only {page['controls']} controls in the panel")
    return problems


results, failures = {}, []
for scale in (75, 100, 125, 150):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        measure, profile = root / "measure", root / "profile"
        measure.mkdir()
        profile.mkdir()
        (profile / "settings.json").write_text(json.dumps({"schemaVersion": 1, "window": {"scalePercent": scale}}))
        env = {**os.environ, "OFFWORK_MEASURE": str(measure), "OFFWORK_PROFILE_DIR": str(profile),
               "HOME": str(root), "XDG_CONFIG_HOME": str(root / ".config")}
        app = subprocess.Popen([str(binary)], env=env)
        try:
            wait(measure / "frame", app)
            (measure / "layout-check").touch()
            wait(measure / "layout.json", app, 60)
            report = json.loads((measure / "layout.json").read_text(encoding="utf-8"))
        finally:
            app.terminate()
            app.wait(timeout=15)
    problems = check(report)
    results[f"{scale}%"] = {"ok": not problems, "problems": problems, **report}
    print(f"{label} {scale}%: window {report['window']} work {report['work_area']} "
          f"panel {report['page']['panel']} -> {'ok' if not problems else problems}")
    failures += [f"{scale}%: {p}" for p in problems]

Path("artifacts").mkdir(exist_ok=True)
Path(f"artifacts/layout-{label}.json").write_text(json.dumps(results, indent=2) + "\n")
if failures:
    raise SystemExit("\n".join(failures))
