"""Windows install, upgrade, migration and compatibility smoke on a CI runner.

1. Install the real OffWork 0.4.5 (C++) from GitHub with a hand-written INI.
2. Install the new Rust OffWork-Setup.exe over it, silently. The installer
   must close 0.4.5 through its window class, keep AppId and relaunch.
3. The Rust app must hold the legacy mutex and window class, migrate the INI
   to settings.json exactly, and leave the INI byte-for-byte unchanged.
4. A second launch must exit and leave one instance; a second (same-version)
   install must close the Rust app the same way and relaunch it.
5. Silent uninstall closes it and removes the program.
Results go to artifacts/windows-smoke.json.
"""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.request
import winreg

LEGACY_SETUP = "https://github.com/Token-Rap/OffWork/releases/download/v0.4.5/OffWork-Setup.exe"
CLASS = "OffWork.Native.Widget"
UNINSTALL = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\{8F3A1C2E-5B47-4D9A-9E61-7C0D2A6F4B83}_is1"
user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
user32.FindWindowW.restype = ctypes.c_void_p
kernel32.OpenMutexW.restype = ctypes.c_void_p

local = Path(os.environ["LOCALAPPDATA"])
data = local / "OffWork"
program = local / "Programs" / "OffWork" / "OffWork.exe"
results: dict = {}


def wait(condition, label: str, seconds: float = 120) -> float:
    start = time.monotonic()
    while not condition():
        if time.monotonic() - start > seconds:
            raise RuntimeError(f"timed out: {label}")
        time.sleep(0.2)
    return round(time.monotonic() - start, 1)


def window_pid() -> int | None:
    handle = user32.FindWindowW(CLASS, "OffWork")
    if not handle:
        return None
    pid = ctypes.c_ulong()
    user32.GetWindowThreadProcessId(ctypes.c_void_p(handle), ctypes.byref(pid))
    return pid.value


def mutex_exists() -> bool:
    handle = kernel32.OpenMutexW(0x00100000, False, r"Local\OffWork.Native.SingleInstance")
    if handle:
        kernel32.CloseHandle(ctypes.c_void_p(handle))
    return bool(handle)


def processes() -> list[str]:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq OffWork.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    return [line for line in out.splitlines() if line.startswith('"OffWork.exe"')]


def install(setup: Path, label: str) -> None:
    start = time.monotonic()
    code = subprocess.run([str(setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"], timeout=300).returncode
    if code:
        raise RuntimeError(f"{label} installer exited {code}")
    results[f"{label}_install_seconds"] = round(time.monotonic() - start, 1)


def main(new_setup: Path) -> None:
    data.mkdir(parents=True, exist_ok=True)
    ini = data / "settings.ini"
    ini.write_text(
        "[resignation]\r\ndate=2027-03-15\r\nshow=1\r\n[window]\r\nhideEarned=1\r\nscalePercent=125\r\nopacityPercent=60\r\n"
        "[work]\r\nstartMinutes=1320\r\nendMinutes=360\r\nendInputMode=endTime\r\nlunchStartMinutes=1410\r\n"
        "lunchEndMinutes=30\r\ndeductLunch=1\r\ndailySalary=1234.57\r\n",
        encoding="ascii",
    )
    legacy = Path(os.environ["RUNNER_TEMP"]) / "OffWork-0.4.5-Setup.exe"
    urllib.request.urlretrieve(LEGACY_SETUP, legacy)
    install(legacy, "legacy_0_4_5")
    results["legacy_running_after"] = wait(lambda: window_pid() is not None, "0.4.5 window")
    legacy_pid = window_pid()
    ini_bytes = ini.read_bytes()
    legacy_size = program.stat().st_size

    install(new_setup, "rust_upgrade")
    wait(lambda: program.stat().st_size != legacy_size, "program replaced")
    results["rust_running_after"] = wait(lambda: window_pid() not in (None, legacy_pid), "Rust compat window")
    rust_pid = window_pid()
    assert mutex_exists(), "Rust app must hold the legacy mutex"
    results["program_bytes"] = program.stat().st_size
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL) as key:
        results["uninstall_display_name"] = winreg.QueryValueEx(key, "DisplayName")[0]

    settings = data / "settings.json"
    wait(settings.exists, "settings.json")
    migrated = json.loads(settings.read_text(encoding="utf-8"))
    expected = {
        "schemaVersion": 1,
        "work": {"startMinutes": 1320, "endMinutes": 360, "endInputMode": "endTime", "lunchStartMinutes": 1410,
                 "lunchEndMinutes": 30, "dailySalary": 1234.57, "deductLunch": True},
        "window": {"scalePercent": 125, "opacityPercent": 60, "hideEarned": True},
        "resignation": {"date": "2027-03-15", "show": True},
    }
    assert migrated == expected, migrated
    assert ini.read_bytes() == ini_bytes, "INI must stay untouched for rollback"
    results["migration"] = "exact, INI unchanged"

    # Second launch: re-activates the running instance and exits.
    second = subprocess.run([str(program)], timeout=60)
    results["second_launch_exit_code"] = second.returncode
    time.sleep(2)
    assert window_pid() == rust_pid and len(processes()) == 1, processes()

    # Re-install the same build: the installer closes the Rust app the same
    # way it closed 0.4.5 (WM_ENDSESSION to the legacy window) and relaunches.
    install(new_setup, "rust_reinstall")
    results["relaunched_after"] = wait(lambda: window_pid() not in (None, rust_pid), "relaunch")
    assert json.loads(settings.read_text(encoding="utf-8")) == expected

    uninstaller = program.parent / "unins000.exe"
    code = subprocess.run([str(uninstaller), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"], timeout=120).returncode
    results["uninstall_exit_code"] = code
    wait(lambda: window_pid() is None and not program.exists(), "uninstall", 60)
    results["uninstalled"] = True
    results["settings_kept_after_uninstall"] = settings.exists() and ini.exists()


if __name__ == "__main__":
    try:
        main(Path(sys.argv[1]).resolve())
        results["ok"] = True
    finally:
        Path("artifacts").mkdir(exist_ok=True)
        Path("artifacts/windows-smoke.json").write_text(json.dumps(results, indent=2) + "\n")
        print(json.dumps(results, indent=2))
