"""Exercise the installed reminder's Windows Shell activation and native tray popup."""
from ctypes import byref, c_void_p, windll, wintypes
import json
import os
from pathlib import Path
import subprocess
import time
from xml.etree import ElementTree

import psutil

user32 = windll.user32
user32.FindWindowW.restype = wintypes.HWND
user32.FindWindowExW.restype = wintypes.HWND


def wait_for(probe, description, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = probe()
        if result:
            return result
        time.sleep(.1)
    raise AssertionError(f"Timed out waiting for {description}")


def window(pid, cls, title=None, visible=False):
    hwnd = None
    while True:
        hwnd = user32.FindWindowExW(None, c_void_p(hwnd), cls, title)
        if not hwnd:
            return None
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(c_void_p(hwnd), byref(owner))
        if owner.value == pid and (not visible or user32.IsWindowVisible(c_void_p(hwnd))):
            return c_void_p(hwnd)


def activate_and_quit(app, measure):
    report = json.loads((measure / "toast.json").read_text(encoding="utf-8"))
    shortcut_id = subprocess.check_output(["powershell", "-NoProfile", "-Command", """
        $folder = (New-Object -ComObject Shell.Application).NameSpace([Environment]::GetFolderPath('Programs'))
        $folder.ParseName('OffWork.lnk').ExtendedProperty('System.AppUserModel.ID')
    """], text=True).strip()
    assert report["aumid"] == shortcut_id == "io.github.token-rap.offwork", report
    toast = ElementTree.fromstring(report["xml"])
    assert toast.get("activationType") == "protocol" and toast.get("launch") == "offwork-reminder:show"
    assert toast.find("visual/binding/text[2]").text == "还有五分钟下班"
    assert not window(app.pid, None, "OffWork", visible=True)
    os.startfile(toast.attrib["launch"])
    wait_for(lambda: window(app.pid, None, "OffWork", visible=True), "protocol restoring hidden window")

    children = psutil.Process(app.pid).children(recursive=True)
    assert any(p.name().lower() == "msedgewebview2.exe" for p in children)
    owner = window(app.pid, "OffWork.Native.Widget")
    # Send the same version-4 shell callback as a right click on the icon,
    # then select the bottom menu item with an ordinary left click.
    assert user32.PostMessageW(owner, 0x8000 + 0x547, 160 | (120 << 16), (1 << 16) | 0x007b)
    popup = wait_for(lambda: window(app.pid, "#32768", visible=True), "native tray popup")
    rect = wintypes.RECT()
    assert user32.GetWindowRect(popup, byref(rect))
    assert user32.SetCursorPos((rect.left + rect.right) // 2, rect.bottom - 12)
    user32.mouse_event(2, 0, 0, 0, 0)
    user32.mouse_event(4, 0, 0, 0, 0)
    assert app.wait(timeout=30) == 0
    _, alive = psutil.wait_procs(children, timeout=30)
    assert not alive, alive
    print("Installed toast", report, "activated hidden widget; clicked Quit and all children exited", flush=True)
