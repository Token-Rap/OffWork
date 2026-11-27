/* Windows compatibility with OffWork 0.4.x (C++):
 *
 * - The named mutex Local\OffWork.Native.SingleInstance: a second launch of
 *   either generation finds it and re-activates the running one.
 * - A hidden top-level window of class OffWork.Native.Widget titled
 *   "OffWork": the old app's second instance calls ShowWindow(SW_RESTORE) on
 *   it, and installer/offwork.iss posts WM_ENDSESSION / WM_CLOSE to it to ask
 *   OffWork to exit before replacing files. This window never becomes
 *   visible; it forwards those requests to Rust instead.
 */
#define UNICODE
#define _UNICODE
#include <windows.h>

#define LEGACY_MUTEX L"Local\\OffWork.Native.SingleInstance"
#define LEGACY_CLASS L"OffWork.Native.Widget"
#define LEGACY_TITLE L"OffWork"
#define LEGACY_RESTORE (WM_APP + 0x548)

typedef void (*offwork_event)(int kind); /* 1 = restore, 2 = exit */
static offwork_event callback = NULL;
static HANDLE mutex = NULL;

/* Returns 1 when this process owns the mutex, 0 when another OffWork was
 * running (it has been asked to restore), -1 on error. */
int offwork_legacy_acquire(void) {
    mutex = CreateMutexW(NULL, TRUE, LEGACY_MUTEX);
    if (!mutex) return -1;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return 1;
    HWND existing = FindWindowW(LEGACY_CLASS, LEGACY_TITLE);
    if (existing) {
        AllowSetForegroundWindow(ASFW_ANY);
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
    }
    CloseHandle(mutex);
    mutex = NULL;
    return 0;
}

static LRESULT CALLBACK legacy_proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS *pos = (WINDOWPOS *)l;
        if (pos->flags & SWP_SHOWWINDOW) {
            pos->flags &= ~SWP_SHOWWINDOW;
            PostMessageW(window, LEGACY_RESTORE, 0, 0);
        }
        return 0;
    }
    case WM_SHOWWINDOW:
        if (w) PostMessageW(window, LEGACY_RESTORE, 0, 0);
        return 0;
    case LEGACY_RESTORE:
        if (IsWindowVisible(window)) ShowWindow(window, SW_HIDE);
        if (callback) callback(1);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (w && callback) callback(2);
        return 0;
    case WM_CLOSE:
        if (callback) callback(2);
        return 0;
    }
    return DefWindowProcW(window, msg, w, l);
}

/* Must run on the thread that pumps messages (the Tauri main thread). */
int offwork_legacy_window(offwork_event on_event) {
    callback = on_event;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = legacy_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = LEGACY_CLASS;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, LEGACY_CLASS, LEGACY_TITLE,
                                  WS_POPUP, -32000, -32000, 0, 0, NULL, NULL, wc.hInstance, NULL);
    return window != NULL;
}

/* Balloon clicks restore through the same path. */
void offwork_legacy_restore(void) {
    if (callback) callback(1);
}
