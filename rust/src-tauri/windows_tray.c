#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#define TRAY_EVENT (WM_APP + 0x547)
enum { SHOW = 1, QUIT, HIDE, AUTOSTART };
void offwork_tray_command(int command);

static NOTIFYICONDATAW icon;
static LONG start_at_login;
static UINT taskbar_created;

static BOOL add_icon(void) {
    if (!Shell_NotifyIconW(NIM_ADD, &icon)) return FALSE;
    icon.uVersion = NOTIFYICON_VERSION_4;
    return Shell_NotifyIconW(NIM_SETVERSION, &icon);
}

static void menu(HWND owner, POINT pt) {
    HMENU popup = CreatePopupMenu();
    if (!popup) return;
    AppendMenuW(popup, MF_STRING, SHOW, L"显示手账");
    AppendMenuW(popup, MF_STRING, HIDE, L"隐藏手账");
    AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
    AppendMenuW(popup, MF_STRING | (start_at_login ? MF_CHECKED : 0), AUTOSTART, L"开机自启");
    AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
    AppendMenuW(popup, MF_STRING, QUIT, L"退出 OffWork");
    if (pt.x == -1 && pt.y == -1) GetCursorPos(&pt);
    SetForegroundWindow(owner);
    UINT selected = TrackPopupMenuEx(popup, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                     pt.x, pt.y, owner, NULL);
    DestroyMenu(popup);
    PostMessageW(owner, WM_NULL, 0, 0);
    if (selected) offwork_tray_command(selected);
}

static LRESULT CALLBACK tray_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l,
                                  UINT_PTR id, DWORD_PTR ref) {
    (void)ref;
    if (msg == TRAY_EVENT) {
        switch (LOWORD(l)) {
        case WM_CONTEXTMENU: {
            POINT anchor = { GET_X_LPARAM(w), GET_Y_LPARAM(w) };
            menu(hwnd, anchor);
            break;
        }
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case NIN_BALLOONUSERCLICK:
            offwork_tray_command(SHOW);
            break;
        }
        return 0;
    }
    if (msg == taskbar_created) add_icon();
    if (msg == WM_NCDESTROY) {
        Shell_NotifyIconW(NIM_DELETE, &icon);
        RemoveWindowSubclass(hwnd, tray_proc, id);
    }
    return DefSubclassProc(hwnd, msg, w, l);
}

int offwork_tray_init(HWND widget, int enabled) {
    HWND owner = FindWindowW(L"OffWork.Native.Widget", L"OffWork");
    if (!owner || !SetWindowSubclass(owner, tray_proc, 1, 0)) return 0;
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    start_at_login = enabled;
    icon.cbSize = sizeof(icon);
    icon.hWnd = owner;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    icon.uCallbackMessage = TRAY_EVENT;
    icon.hIcon = (HICON)SendMessageW(widget, WM_GETICON, ICON_SMALL, 0);
    if (!icon.hIcon) icon.hIcon = (HICON)GetClassLongPtrW(widget, GCLP_HICON);
    if (!icon.hIcon) icon.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    lstrcpyW(icon.szTip, L"OffWork 下班手账");
    return add_icon();
}

void offwork_tray_checked(int enabled) {
    InterlockedExchange(&start_at_login, enabled);
}

// Portable fallback uses the same tray icon and the same restore callback.
int offwork_notify(const WCHAR *message) {
    NOTIFYICONDATAW balloon = icon;
    balloon.uFlags = NIF_INFO;
    balloon.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    lstrcpynW(balloon.szInfoTitle, L"下班手账", ARRAYSIZE(balloon.szInfoTitle));
    lstrcpynW(balloon.szInfo, message, ARRAYSIZE(balloon.szInfo));
    return Shell_NotifyIconW(NIM_MODIFY, &balloon);
}
