"""Drag the checked-in PNG from a GTK source onto the release webview under Xvfb."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time

import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gdk, Gtk, GLib

def xdotool(*args):
    return subprocess.check_output(["xdotool", *map(str,args)],text=True).strip()

with tempfile.TemporaryDirectory() as config:
    folder = Path(config)
    fixture = Path("../tests/data/dingtalk_clockin.png").resolve().as_uri()
    app = subprocess.Popen(["target/release/offwork"],env={**os.environ,"XDG_CONFIG_HOME":config,"OFFWORK_MEASURE":config})
    source = Gtk.Window(title="OffWork fixture source")
    label = Gtk.Label(label="Screenshot")
    surface = Gtk.EventBox()
    surface.add(label)
    source.add(surface)
    source.set_default_size(180,100)
    source.move(20,100)
    target = Gtk.TargetEntry.new("text/uri-list",0,0)
    surface.drag_source_set(Gdk.ModifierType.BUTTON1_MASK,[target],Gdk.DragAction.COPY)
    surface.connect("drag-data-get",lambda widget,context,data,info,stamp:data.set_uris([fixture]))
    source.show_all()
    failures=[]
    def run():
        try:
            deadline=time.monotonic()+45
            while not (folder/"frame").exists():
                if app.poll() is not None or time.monotonic()>deadline: raise RuntimeError("Widget did not render")
                time.sleep(.05)
            xdotool("mousemove","100","150","mousedown","1")
            window=xdotool("search","--onlyvisible","--name","^下班手账$").splitlines()[-1]
            geometry=dict(row.split("=",1) for row in xdotool("getwindowgeometry","--shell",window).splitlines())
            end_x,end_y=int(geometry["X"])+80,int(geometry["Y"])+80
            for step in range(1,21):
                xdotool("mousemove",int(100+(end_x-100)*step/20),int(150+(end_y-150)*step/20))
                time.sleep(.04)
            xdotool("mouseup","1")
            settings=folder/"io.github.token-rap.offwork.prototype/settings.json"
            for _ in range(200):
                if settings.exists() and json.loads(settings.read_text())["start"]==535: break
                time.sleep(.1)
            else: raise RuntimeError("Native file drop did not persist 08:55")
            print("GTK file drag -> release widget -> Tesseract -> persisted 08:55")
        except Exception as error:
            failures.append(error)
        finally:
            GLib.idle_add(Gtk.main_quit)
    threading.Thread(target=run,daemon=True).start()
    try: Gtk.main()
    finally:
        app.terminate()
        app.wait(timeout=15)
    if failures: raise failures[0]
