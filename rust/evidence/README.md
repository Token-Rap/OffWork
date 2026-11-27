> Phases 3 and 4 (rules, migration, desktop preview): see [PHASE3-4.md](PHASE3-4.md). The tables below are the phase 2 prototype.

# Phase 2 measurements

Commit measured: `6c6d71c`. CI run: Rust desktop prototype #45868890396 (all three jobs green).
Raw numbers live next to this file: `mac-m1-runtime-600s.json` (local) and `ci/*` (CI artifacts).

## Package size (release, unsigned)

| Platform | Artifact | Download | Unpacked | Extra runtime dependencies |
|---|---|---|---|---|
| Windows x64 | `offwork.exe` (portable) | 11.25 MiB | 11.25 MiB | System WebView2 (Evergreen 157 on the runner: 903 MiB on disk, preinstalled on Win 10/11) |
| Windows x64 | `OffWork_0.1.0_x64-setup.exe` (NSIS) | 2.45 MiB | 11.43 MiB | same |
| macOS arm64 | `OffWork_0.1.0_aarch64.dmg` | 3.33 MiB | 8.72 MiB (.app) | none (system WKWebView, Vision) |
| Ubuntu 24.04 x64 | `.deb` | 4.92 MiB | 13.26 MiB | libwebkit2gtk-4.1-0 91.2 MiB installed (its own transitive deps not counted), libayatana-appindicator3-1 87 KiB; OCR: tesseract-ocr 1.1 MiB + chi-sim 2.4 MiB + eng 3.9 MiB |
| Ubuntu 24.04 x64 | `.AppImage` | 79.8 MiB | 257.7 MiB | bundles WebKitGTK; Tesseract still from the system |

SHA-256 values are in `ci/*-sizes.json`.

## Runtime (process tree: app + WebView helper processes)

| Where | Launch → 2nd rAF | Visible RSS avg / peak | Visible CPU | Hidden RSS avg / peak | Hidden CPU | Window |
|---|---|---|---|---|---|---|
| **macOS 26.5, Apple M1 8 GB (this Mac)** | 2029 ms first launch after build; 916 ms median of 5 relaunches (825–1610) | 100.0 / 169.3 MiB | 1.65 % | 62.0 / 87.1 MiB | 0.09 % | 600 s each |
| macOS CI (macos-latest arm64) | 7688 ms | 178.9 / 180.0 MiB | 2.46 % | 179.9 / 180.1 MiB | 0.16 % | 30 s each |
| Windows CI (windows-latest) | 9925 ms | 305.5 / 312.6 MiB | 2.03 % | 312.5 / 313.1 MiB | 1.88 % | 30 s each |
| Windows CI, legacy C++ 0.4.5 same runner | 212 ms (visible window) | 23.6 MiB | 0.16 % | 23.6 MiB | 0.05 % | 30 s each |
| Ubuntu CI, Xvfb software rendering | 562 ms | 484.3 / 484.5 MiB | 1.17 % | 482.4 / 484.5 MiB | 0.10 % | 30 s each |

CPU is percent of one core. CI runners are shared VMs without a GPU; treat their numbers as smoke values, not budgets.

## Verified paths

- Rust core rules: CTest inputs/outputs ported for schedule, lunch, earnings, resignation workdays, reminder and OCR text parsing; green on all three OSes.
- Native OCR on the DingTalk fixture → 08:55 (`ocr::native_fixture`): macOS Vision, Windows.Media.Ocr, Linux Tesseract, all green in CI.
- Real drag-in: Linux CI drags the fixture from a GTK source into the release widget under Xvfb (`scripts/x11_drop.py`) and checks the persisted 08:55; on this Mac an AppKit drag source with real pointer events (`scripts/mac_drop.swift`) dropped the fixture on the widget, Vision read it and settings.json changed from 540 to 535.
- Close-to-tray on macOS: × hides the window and the process keeps running; with the window hidden, usernoted recorded 「还有五分钟下班」 at exactly hh:mm:00 for three test schedules and 「下班时间到了」 at the shift end (10:08:00 / 10:13:00 for the 10:06–10:13 shift), after granting the first-run notification permission.

## Still needs a real desktop

- Notification click → restore window (macOS, Windows balloon, Linux) and banner visibility after permission prompts.
- Windows: real-desktop cold start, 10-minute visible/hidden memory and CPU; hidden CPU on CI does not drop (WebView2 keeps ticking), needs a check on a real session.
- Linux GNOME Wayland/X11 and KDE: tray menu, always-on-top refusal fallback, GPU-backed memory numbers.
- Multi-monitor / DPI, calendar popover placement on all three engines.
