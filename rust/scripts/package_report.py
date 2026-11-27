"""Stage release-named artifacts, record exact sizes, and write SHA256SUMS.

Asset names match what the in-app updater looks for (src-tauri/src/updater.rs)
and, on Windows, what OffWork 0.4.x's updater looks for (OffWork-Setup.exe).
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

release = Path("target/release")
bundle = release / "bundle"
out = Path("artifacts")
out.mkdir(exist_ok=True)
results: dict = {}


def size(path: Path) -> int:
    if path.is_file():
        return path.stat().st_size
    return sum(p.stat().st_size for p in path.rglob("*") if p.is_file() and not p.is_symlink())


def stage(file: Path, name: str, payload: Path) -> None:
    destination = out / name
    shutil.copy2(file, destination)
    results[name] = {
        "download_bytes": size(destination),
        "download_mib": round(size(destination) / 1048576, 2),
        "unpacked_bytes": size(payload),
        "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
    }


if sys.platform == "win32":
    exe = release / "offwork.exe"
    stage(exe, "OffWork.exe", exe)
    setup = Path("target/installer/OffWork-Setup.exe")
    with tempfile.TemporaryDirectory() as temp:
        subprocess.run(["7z", "x", str(setup), "-o" + temp], check=True, stdout=subprocess.DEVNULL)
        stage(setup, "OffWork-Setup.exe", Path(temp))
elif sys.platform == "darwin":
    stage(next(bundle.glob("dmg/*.dmg")), "OffWork-macOS-arm64.dmg", next(bundle.glob("macos/*.app")))
else:
    with tempfile.TemporaryDirectory() as temp:
        deb = next(bundle.glob("deb/*.deb"))
        subprocess.run(["dpkg-deb", "-x", deb, temp], check=True)
        stage(deb, "OffWork-linux-amd64.deb", Path(temp))
    with tempfile.TemporaryDirectory() as temp:
        image = next(bundle.glob("appimage/*.AppImage"))
        subprocess.run([image.resolve(), "--appimage-extract"], cwd=temp, stdout=subprocess.DEVNULL, check=True)
        stage(image, "OffWork-linux-x86_64.AppImage", Path(temp) / "squashfs-root")

(out / "SHA256SUMS").write_text("".join(f"{v['sha256']}  {k}\n" for k, v in sorted(results.items())))
(out / "sizes.json").write_text(json.dumps(results, indent=2) + "\n")
print(json.dumps(results, indent=2))
