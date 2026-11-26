#include "resignation_calendar.h"

#include "app_edit.h"
#include "app_view.h"
#include "resignation.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <algorithm>
#include <cwchar>
#include <utility>

namespace offwork {
namespace {
constexpr wchar_t kCalendarClass[] = L"OffWork.ResignationCalendar";
constexpr int kClear = 100;
HWND popup = nullptr;
HWND calendar = nullptr;
HFONT font = nullptr;
RECT previousMonth{};
RECT nextMonth{};

void PaintMonthNavigation(HDC dc) {
    // Keep native hit testing/navigation, but cover the legacy white arrow faces.
    for (const bool next : {false, true}) {
        RECT rect = next ? nextMonth : previousMonth;
        if (IsRectEmpty(&rect)) continue;
        SetDCBrushColor(dc, kButtonBackground);
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const auto oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kAccent);
        DrawTextW(dc, next ? L"›" : L"‹", 1, &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    }
}

void Finish(bool restoreFocus) {
    CloseResignationCalendar();
    if (restoreFocus && IsWindowVisible(g.window)) {
        SetForegroundWindow(g.window);
        SetFocus(g.window);
        SetFocusedField(5);
        Present();
    }
}

LRESULT CALLBACK ChildProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR) {
    if (window == calendar && (message == WM_PAINT || message == WM_PRINTCLIENT || message == WM_PRINT)) {
        const auto result = DefSubclassProc(window, message, wParam, lParam);
        const HDC dc = message == WM_PAINT ? GetDC(window) : reinterpret_cast<HDC>(wParam);
        if (dc != nullptr) {
            const int saved = SaveDC(dc);
            PaintMonthNavigation(dc);
            if (saved != 0) RestoreDC(dc, saved);
            if (message == WM_PAINT) ReleaseDC(window, dc);
        }
        return result;
    }
    if (message == WM_GETDLGCODE && (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_TAB))
        return DLGC_WANTALLKEYS;
    if (message == WM_KEYDOWN) {
        if (wParam == VK_ESCAPE) {
            PostMessageW(popup, WM_COMMAND, IDCANCEL, 0);
            return 0;
        }
        if (wParam == VK_RETURN) {
            const int action = window == calendar ? IDOK : GetDlgCtrlID(window);
            PostMessageW(popup, WM_COMMAND, action, 0);
            return 0;
        }
        if (wParam == VK_TAB) {
            SetFocus(GetNextDlgTabItem(popup, window, (GetKeyState(VK_SHIFT) & 0x8000) != 0));
            return 0;
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK CalendarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND: {
            const HDC dc = reinterpret_cast<HDC>(wParam);
            RECT rect{};
            GetClientRect(window, &rect);
            SetDCBrushColor(dc, kPanelBackground);
            FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            SetDCBrushColor(dc, kInputBackground);
            FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            return 1;
        }
        case WM_DRAWITEM: {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item->CtlType != ODT_BUTTON) break;
            const bool pressed = (item->itemState & ODS_SELECTED) != 0;
            SetDCBrushColor(item->hDC, pressed ? kButtonHover : kButtonBackground);
            FillRect(item->hDC, &item->rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            const auto previousFont = SelectObject(item->hDC, font);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, kTextPrimary);
            RECT textRect = item->rcItem;
            DrawTextW(item->hDC, item->CtlID == kClear ? L"清空日期" : L"取消 (Esc)", -1,
                &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if ((item->itemState & ODS_FOCUS) != 0) {
                RECT focus = item->rcItem;
                InflateRect(&focus, -3, -3);
                DrawFocusRect(item->hDC, &focus);
            }
            SelectObject(item->hDC, previousFont);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == kClear) {
                SYSTEMTIME date{};
                if (LOWORD(wParam) == IDOK && !MonthCal_GetCurSel(calendar, &date)) return 0;
                wchar_t text[16]{};
                if (LOWORD(wParam) == IDOK)
                    swprintf_s(text, L"%04u-%02u-%02u", date.wYear, date.wMonth, date.wDay);
                g.fields[5] = MakeField(text);
                ApplyFieldChange(5); // Same persistence/refresh path as typed dates.
                Finish(true);
            } else if (LOWORD(wParam) == IDCANCEL) {
                Finish(true);
            }
            return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lParam)->code == MCN_SELECT)
                PostMessageW(window, WM_COMMAND, IDOK, 0);
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) CloseResignationCalendar();
            return 0;
        case WM_DPICHANGED:
            // Cancel an in-progress choice when moved to another monitor;
            // reopening measures the control using the owner's current DPI.
            Finish(false);
            return 0;
        case WM_CLOSE:
            Finish(true);
            return 0;
        case WM_NCDESTROY:
            popup = nullptr;
            calendar = nullptr;
            if (font != nullptr) DeleteObject(font);
            font = nullptr;
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}  // namespace

void CloseResignationCalendar() {
    if (const HWND closing = std::exchange(popup, nullptr)) DestroyWindow(closing);
}

void ShowResignationCalendar() {
    if (popup != nullptr) {
        SetFocus(calendar);
        return;
    }
    SetFocusedField(-1); // Commit any typed draft before locating the saved date.
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_DATE_CLASSES};
    if (!InitCommonControlsEx(&controls)) return;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = CalendarProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // WM_ERASEBKGND uses the shared application palette.
    wc.lpszClassName = kCalendarClass;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    RECT anchor = ScaleRect(kResignationDateBox, g.settings.uiScalePercent, g.dpi);
    MapWindowPoints(g.window, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
    popup = CreateWindowExW(WS_EX_TOOLWINDOW, kCalendarClass, L"选择离职日期",
        WS_POPUP, anchor.left, anchor.top, 1, 1, g.window, nullptr, instance, nullptr);
    if (popup == nullptr) return;
    const auto px = [](int value) { return ScaleValue(value, g.settings.uiScalePercent, g.dpi); };
    font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    calendar = CreateWindowExW(0, MONTHCAL_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        MCS_NOTODAYCIRCLE, px(8), px(8), 1, 1, popup, nullptr, instance, nullptr);
    if (calendar == nullptr) { CloseResignationCalendar(); return; }
    // Visual styles override MonthCal colors; disable them only on this control.
    SetWindowTheme(calendar, L"", L"");
    MonthCal_SetColor(calendar, MCSC_BACKGROUND, kPanelBackground);
    MonthCal_SetColor(calendar, MCSC_MONTHBK, kPanelBackground);
    MonthCal_SetColor(calendar, MCSC_TITLEBK, kTitleBackground);
    MonthCal_SetColor(calendar, MCSC_TITLETEXT, kTextPrimary);
    MonthCal_SetColor(calendar, MCSC_TEXT, kTextPrimary);
    MonthCal_SetColor(calendar, MCSC_TRAILINGTEXT, kTextMuted);
    SendMessageW(calendar, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SYSTEMTIME range[2]{};
    range[0].wYear = 1601; range[0].wMonth = 1; range[0].wDay = 1;
    range[1].wYear = 9999; range[1].wMonth = 12; range[1].wDay = 31;
    MonthCal_SetRange(calendar, GDTR_MIN | GDTR_MAX, range);
    SYSTEMTIME date{};
    GetLocalTime(&date);
    if (const auto saved = ParseResignationDate(g.settings.resignationDate)) {
        const std::chrono::year_month_day ymd{*saved};
        date.wYear = static_cast<WORD>(static_cast<int>(ymd.year()));
        date.wMonth = static_cast<WORD>(static_cast<unsigned>(ymd.month()));
        date.wDay = static_cast<WORD>(static_cast<unsigned>(ymd.day()));
        if (date.wYear < 1601) date = range[0];
    }
    MonthCal_SetCurSel(calendar, &date);
    RECT size{};
    MonthCal_GetMinReqRect(calendar, &size);
    const int width = std::max(static_cast<int>(size.right), px(240));
    const int height = size.bottom;
    SetWindowPos(calendar, nullptr, px(8), px(8), width, height, SWP_NOZORDER);
    previousMonth = nextMonth = RECT{};
    for (int y = 0; y < height / 3; ++y) {
        for (int x = 0; x < width; ++x) {
            MCHITTESTINFO hit{};
            hit.cbSize = MCHITTESTINFO_V1_SIZE;
            hit.pt = {x, y};
            const auto area = MonthCal_HitTest(calendar, &hit);
            RECT* target = area == MCHT_TITLEBTNPREV ? &previousMonth :
                (area == MCHT_TITLEBTNNEXT ? &nextMonth : nullptr);
            if (target == nullptr) continue;
            if (IsRectEmpty(target)) *target = {x, y, x + 1, y + 1};
            else {
                target->left = std::min(target->left, static_cast<LONG>(x));
                target->top = std::min(target->top, static_cast<LONG>(y));
                target->right = std::max(target->right, static_cast<LONG>(x + 1));
                target->bottom = std::max(target->bottom, static_cast<LONG>(y + 1));
            }
        }
    }
    SetWindowSubclass(calendar, ChildProc, 1, 0);
    for (const int id : {kClear, IDCANCEL}) {
        const bool clear = id == kClear;
        HWND button = CreateWindowExW(0, L"BUTTON", clear ? L"清空日期" : L"取消 (Esc)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            clear ? px(8) : width - px(104) + px(8), height + px(16), px(104), px(30),
            popup, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (button == nullptr) { CloseResignationCalendar(); return; }
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetWindowSubclass(button, ChildProc, 1, 0);
    }
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT bounds{0, 0, width + px(16), height + px(54)};
    AdjustWindowRectExForDpi(&bounds, WS_POPUP, FALSE, WS_EX_TOOLWINDOW, g.dpi);
    const int w = bounds.right - bounds.left;
    const int h = bounds.bottom - bounds.top;
    const RECT work = monitor.rcWork;
    const int x = std::clamp(anchor.left, work.left, std::max(work.left, work.right - w));
    const LONG preferredY = anchor.bottom + h <= work.bottom ? anchor.bottom : anchor.top - h;
    const int y = std::clamp(preferredY, work.top, std::max(work.top, work.bottom - h));
    SetWindowPos(popup, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW);
    SetFocus(calendar);
}

}  // namespace offwork
