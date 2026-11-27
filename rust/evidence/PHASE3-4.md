# Phases 3 and 4: rules, migration and desktop preview

CI run measured: Rust desktop preview [#45923631942](https://github.com/Token-Rap/OffWork/actions/runs/45923631942), with all three jobs green. Raw JSON and checksums are in `ci/phase4/`. The local checks ran on macOS 26.5 on an M1.

Status key:
- **CI**: verified by an automated test or smoke on the named runners.
- **Mac**: exercised by hand on a real macOS desktop with real pointer and keyboard events.
- **Real desktop**: still needs a person on that system. Nothing below was faked.

## Phase 3: rules and migration

**`offwork-core`** ports every pure rule from the C++ app, and each old CTest case is a fixed fixture. The suite has 60 core tests, 4 shell OCR tests and 6 vitest tests, and they run on windows-latest, macos-latest (arm64) and ubuntu-24.04.

| Old CTest entry | Rust test file | Notes |
|---|---|---|
| `calculator` | `core/tests/calculator.rs` | Every lunch second is frozen, the overnight and invalid lunch matrix is covered, and the display countdown is separate from the real one. |
| `reminder` | `core/tests/reminder.rs` | Every C++ reminder case, plus a wake from sleep inside the grace window. |
| `settings`, `resignation` (INI parts) | `core/tests/settings.rs`, `resignation.rs` | Runs through a stand-in for `WritePrivateProfileStringW` and the C++ `SaveSettings`. On Windows, **every INI fixture is also read with the real `GetPrivateProfile*W` and must match**. This cross-check found one difference: Windows ignores a UTF-8 BOM. The fix is commit `fix: match Windows on UTF-8 BOM INIs`. |
| `money_display`, `image_file`, `autostart` | `core/tests/small.rs` | Also covers a Windows Run-key round trip on a scratch HKCU key, and a LaunchAgent / XDG entry round trip in a temporary HOME. |
| `update` | `core/tests/update.rs` | Semver, the release Location and JSON parsers (duplicate keys are rejected), the backoff, the progress percentage, the host allow list, redirects and the SHA256SUMS parser. |
| `app_edit`, `app_resignation` (editor rules) | `core/tests/app_edit.rs` | The same sequences as the C++ tests, run through the pure `edit` module. They cover net hours, saving on focus loss, invalid edits reverting, the overnight lunch pair, OCR fill and conflicts, and the resignation date. |
| `app_calendar` (placement) | `core/tests/placement.rs` | 18 combinations of DPI, scale and corner, a negative-coordinate secondary monitor, small work areas that need scrolling, and keeping the widget inside the work area when it expands. |
| `ocr_time` | `core/tests/ocr.rs` | Unchanged from phase 2. |
| `ocr_image`, `drop_target` | `src-tauri/src/ocr.rs` tests, `scripts/x11_drop.py` | Native OCR on the DingTalk fixture on all three OSes. Error paths are tested: multiple files, a folder, a wrong type, a missing file, an empty file, a file over 20 MiB, a corrupt image, and an image with no time. Linux also runs a real GTK drag into the release widget. |

**Migration** (`core/src/store.rs`, `core/tests/migration.rs`):
- The app reads `%LOCALAPPDATA%\OffWork\settings.ini`, falling back to `%WINDIR%\OffWork.ini` like the profile API does. It writes `settings.json` with `schemaVersion: 1` in the same folder, through a temporary file, flush, rename, re-read and compare.
- Tested in temporary user folders:
  - exact import of every key;
  - the INI is left byte-for-byte unchanged, so rolling back to 0.4.5 still works;
  - a relaunch does nothing more, and later INI changes are ignored;
  - a failed write leaves no `settings.json`, keeps the INI values in memory, and is retried on the next save or launch;
  - temporary files left by a crash do no harm;
  - a corrupt JSON is backed up and re-imported;
  - a newer schema is never overwritten (read-only mode);
  - an unreadable file is kept and retried;
  - unknown and legacy keys (`lunchDurationMinutes`).
- **CI on Windows (`scripts/windows_smoke.py`):**
  - Install the published **0.4.5** `OffWork-Setup.exe` with a hand-written INI.
  - Silently install the new Rust `OffWork-Setup.exe` over it. The installer closes 0.4.5 through its window class, keeps the AppId, and relaunches the app.
  - The migrated JSON matches the INI exactly, and the INI is unchanged.
  - A second launch exits with code 0 and leaves one instance.
  - Reinstalling closes the Rust app through the same legacy window and relaunches it.
  - The silent uninstall removes the program and leaves the user settings.

## Phase 4: feature-parity checklist

| PLAN row | Done | Verified |
|---|---|---|
| 时钟 | Cat icon, local date and time updating every second, before / working / lunch / finished states, overnight shifts | **CI** (core). **Mac** screenshots. |
| 排班 | Fixed end or net-hours mode, lunch start and end, salary. Live save of complete end and lunch input. Enter commits, Esc reverts, Tab and Shift+Tab move, field filters match the C++ ones | **CI** (core and vitest filters). **Mac**: typed `08:3x0` + Tab saved 08:30 and moved the end to 17:30 at 7.5 net hours; typed 999 + Esc reverted and did not save. Copy and paste, and IME, use the native inputs; **real desktop** on Windows and Linux. |
| 进度与金额 | Progress and earnings pause at lunch; eye toggle shows `****` and is saved; deducting lunch only changes the display | **CI** (core and migration of `hideEarned`). |
| 离职 | Toggle and date are independent; workdays in (today, date]; hidden when expired or invalid | **CI**. **Mac**: 12-04 on 11-26 shows 剩余 6 天离职. |
| 日历 | Owned popup window: opens below, flips above, clamps to the work area, scrolls; Enter or click picks, Esc or clicking away cancels, 清空日期; F4 or ↓ opens it; arrows, PgUp / PgDn and Shift+PgUp / PgDn move. Inline scrollable calendar on Wayland. A DPI change closes the popup | **CI** (placement and keyboard model). **Mac**: the popup flipped above the field inside the work area; → + Enter saved 11-27; Esc cancelled. **Real desktop**: Windows and Linux popups, multiple monitors with negative coordinates, real DPI moves, Wayland inline mode. |
| 悬浮窗 | Starts at the work area's top right, pinned and collapsed. Drag by the title. Pin toggle. Scale 75–150 % with a slider and a corner grip. Opacity 25–100 % (transparent window). Expanding stays inside the work area. Version shown collapsed and expanded | **CI** (geometry). **Mac**: the slider set 90 % to 324×699 and saved it; opacity 40 % showed the desktop through the window. **Real desktop**: Windows and Linux transparency, dragging across monitors with different DPI, the grip on every engine. |
| 常驻 | Single instance; × hides to the tray, or quits when there is no tray. Tray offers show, hide, 开机自启 (read back from the system) and quit; click or double-click restores. On Windows: legacy mutex `Local\OffWork.Native.SingleInstance` and window class `OffWork.Native.Widget` | **CI** on Windows: second launch, installer close, reinstall and uninstall. **Mac**: × hid the window and the process kept running; the 开机自启 switch wrote the LaunchAgent (temporary HOME). **Real desktop**: login actually starting the app on each OS, tray rebuild after an Explorer restart, GNOME and KDE tray menus, keyboard restore from the tray. |
| 提醒 | 5 minutes before and at the end, from the real remaining time; silent at startup and after edits; stale crossings skipped; sleep and wake handled by recomputing every second | **CI** on all three OSes (`scripts/reminder_smoke.py`): with the widget hidden, 还有五分钟下班 fired once on the exact second, and a relaunch inside the window stayed silent. **Real desktop**: clicking the notification to restore the window (wired through the plugin `on_action` and the Windows balloon `NIN_BALLOONUSERCLICK`), and banner visibility. On this Mac the new bundle id `io.github.token-rap.offwork` is waiting at the macOS first-run notification permission prompt, which was left for a person to answer. |
| 截图 | Whole-window drop target with no text; validation; native OCR (Vision, Windows.Media.Ocr, Tesseract); readable errors that expand the settings and also notify; the schedule is kept on any error | **CI** (fixture and error paths on three OSes; real drag on Linux). **Real desktop**: drag on Windows. Missing-language-pack text on real machines. |
| 更新 | Background check of `/releases/latest` (redirect only, then API), every 6 h after success, 10→320 min backoff after failure. Download only on click: allow-listed HTTPS, manual redirects, size limit, exact length, SHA-256 against the release's `SHA256SUMS`, then MZ check on Windows. Windows runs the Inno setup silently; macOS opens the DMG; an AppImage replaces itself; a .deb opens the release page | **CI** (all parsing and policy). **Not exercised end to end**: no release was published, as instructed. **Needs the owner**: a published preview or stable release to test the download, and 0.4.5's built-in updater picking up the new `OffWork-Setup.exe`. |
| UI direction A | 下班手账 paper look, Chinese labels, visible focus rings, labelled controls, `aria-pressed`, `aria-expanded`, radio group, progressbar, alert and status roles; no announcement every second; transitions only with `prefers-reduced-motion: no-preference` | **Mac** screenshots. Screen readers (VoiceOver, Narrator, Orca) on a **real desktop**. |

## Size and runtime (CI, release, unsigned)

| Platform | Asset | Download | Unpacked |
|---|---|---|---|
| Windows x64 | `OffWork.exe` (portable) | 11.78 MiB | 11.78 MiB |
| Windows x64 | `OffWork-Setup.exe` (Inno, same AppId) | 4.53 MiB | 11.78 MiB |
| macOS arm64 | `OffWork-macOS-arm64.dmg` | 3.52 MiB | 9.16 MiB |
| Ubuntu 24.04 | `OffWork-linux-amd64.deb` | 5.15 MiB | 13.77 MiB |
| Ubuntu 24.04 | `OffWork-linux-x86_64.AppImage` | 80.27 MiB | 258.9 MiB |

The portable EXE grew 0.53 MiB since phase 2, mostly from the updater's TLS and HTTP client. It is still above the old 10 MiB cap.

Runtime over 30 s per state, measured on the whole process tree:

| Runner | Launch to first frame | Visible RSS / CPU | Hidden RSS / CPU |
|---|---|---|---|
| Windows | 512 ms | 302.0 MiB / 1.88 % | 309.0 MiB / 1.15 % |
| macOS | 659 ms | 173.8 MiB / 2.24 % | 174.4 MiB / 0.29 % |
| Ubuntu, Xvfb | 694 ms | 492.3 MiB / 1.30 % | 490.6 MiB / 0.03 % |

## Still needs a real desktop, a signature or the owner

- **Windows real machine:**
  - Cold start, and the 10-minute visible and hidden numbers.
  - Hidden CPU stays near 1 % on CI.
  - Balloon click to restore.
  - Transparency and DPI.
  - The 0.4.5 in-app updater to the new installer, which needs a published release.
- **Linux:**
  - GNOME on Wayland and on X11, and KDE: tray menus, refusal to stay on top, the Wayland fallback (normal window with the inline calendar), and notification clicks.
- **macOS:**
  - Answer the notification permission prompt for the new bundle id, then click a notification.
  - Real login through the LaunchAgent.
  - Spaces and multiple monitors.
- **Signing:**
  - Authenticode for the EXE and Setup.
  - Developer ID, notarization and stapling on macOS.
  - An update-signing key (today only the HTTPS host and the SHA-256 checksum are checked).
- **Screen readers and IME** on all three systems.
