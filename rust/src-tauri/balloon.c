#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#define REMINDER_MESSAGE (WM_APP + 0x547)
#define REMINDER_ICON 0x547

static void remove_balloon(HWND window) {
    NOTIFYICONDATAW icon = {0};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = REMINDER_ICON;
    Shell_NotifyIconW(NIM_DELETE, &icon);
}

static LRESULT CALLBACK on_balloon(HWND window, UINT msg, WPARAM w, LPARAM l,
                                   UINT_PTR id, DWORD_PTR context) {
    (void)context;
    if (msg == REMINDER_MESSAGE && w == REMINDER_ICON) {
        switch (l) {
        case NIN_BALLOONUSERCLICK:
            ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
            /* fall through */
        case NIN_BALLOONHIDE:
        case NIN_BALLOONTIMEOUT:
            remove_balloon(window);
            break;
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        remove_balloon(window);
        RemoveWindowSubclass(window, on_balloon, id);
    }
    return DefSubclassProc(window, msg, w, l);
}

// Shell balloons work in portable mode without an installed AppUserModelID.
// The caller must run this on the thread which owns the window.
int offwork_notify(HWND window, const WCHAR *message) {
    if (!SetWindowSubclass(window, on_balloon, REMINDER_ICON, 0)) return 0;
    NOTIFYICONDATAW icon = {0};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = REMINDER_ICON;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
    icon.uCallbackMessage = REMINDER_MESSAGE;
    icon.hIcon = (HICON)SendMessageW(window, WM_GETICON, ICON_SMALL, 0);
    if (!icon.hIcon) icon.hIcon = LoadIconW(NULL, IDI_INFORMATION);
    lstrcpynW(icon.szTip, L"OffWork", ARRAYSIZE(icon.szTip));
    lstrcpynW(icon.szInfoTitle, L"下班手账", ARRAYSIZE(icon.szInfoTitle));
    lstrcpynW(icon.szInfo, message, ARRAYSIZE(icon.szInfo));
    icon.dwInfoFlags = NIIF_INFO;
    if (Shell_NotifyIconW(NIM_MODIFY, &icon)) return 1;
    return Shell_NotifyIconW(NIM_ADD, &icon);
}
