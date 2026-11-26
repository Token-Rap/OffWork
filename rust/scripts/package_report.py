"""Stage download artifacts and report exact bytes of each package and payload."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

release = Path("target/release")
bundle = release/"bundle"
out = Path("artifacts")
out.mkdir(exist_ok=True)
results = {}

def size(path):
    if path.is_file():
        return path.stat().st_size
    return sum(p.stat().st_size for p in path.rglob("*") if p.is_file() and not p.is_symlink())

def stage(file, payload):
    destination = out/file.name
    shutil.copy2(file,destination)
    results[file.name] = {"download_bytes":size(file),"unpacked_bytes":size(payload),"sha256":hashlib.sha256(file.read_bytes()).hexdigest()}

if sys.platform == "win32":
    exe = release/"offwork.exe"
    stage(exe,exe)
    with tempfile.TemporaryDirectory() as temp:
        installer = next(bundle.glob("nsis/*.exe"))
        subprocess.run(["7z","x",str(installer),"-o"+temp],check=True,stdout=subprocess.DEVNULL)
        stage(installer,Path(temp))
elif sys.platform == "darwin":
    stage(next(bundle.glob("dmg/*.dmg")),next(bundle.glob("macos/*.app")))
else:
    with tempfile.TemporaryDirectory() as temp:
        deb = next(bundle.glob("deb/*.deb"))
        subprocess.run(["dpkg-deb","-x",deb,temp],check=True)
        stage(deb,Path(temp))
    with tempfile.TemporaryDirectory() as temp:
        image = next(bundle.glob("appimage/*.AppImage"))
        subprocess.run([image.resolve(),"--appimage-extract"],cwd=temp,stdout=subprocess.DEVNULL,check=True)
        stage(image,Path(temp)/"squashfs-root")
(out/"sizes.json").write_text(json.dumps(results,indent=2)+"\n")
print(json.dumps(results,indent=2))
