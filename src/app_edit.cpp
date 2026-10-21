#include "app_edit.h"

#include "app_tray.h"
#include "app_view.h"
#include "autostart.h"
#include "calculator.h"

#include <cmath>
#include <chrono>
#include <future>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace offwork {

// -- small helpers ----------------------------------------------------------

FieldFilter FilterFor(int fieldIndex) {
    if (fieldIndex == 3) return FieldFilter::Integer;
    return fieldIndex == 2 ||
            (fieldIndex == 1 && g.editEndInputMode == EndInputMode::WorkDuration)
        ? FieldFilter::Decimal
        : FieldFilter::Time;
}

std::size_t MaxLengthFor(int fieldIndex) {
    return fieldIndex == 2 ? kSalaryMaxLength : fieldIndex == 3 ? kLunchMaxLength : kTimeMaxLength;
}

std::wstring FormatTimeValue(int minutes) {
    wchar_t text[16] = {};
    swprintf_s(text, L"%02d:%02d", minutes / 60, minutes % 60);
    return text;
}

std::wstring FormatSalaryValue(double salary) {
    wchar_t text[64] = {};
    swprintf_s(text, L"%.2f", salary);
    std::wstring value(text);
    while (value.size() > 1 && value.back() == L'0') {
        value.pop_back();
    }
    if (!value.empty() && value.back() == L'.') {
        value.pop_back();
    }
    return value;
}

std::wstring FormatDurationHours(int durationMinutes) {
    wchar_t text[32] = {};
    swprintf_s(text, L"%.2f", static_cast<double>(durationMinutes) / 60.0);
    std::wstring value(text);
    while (value.size() > 1 && value.back() == L'0') {
        value.pop_back();
    }
    if (!value.empty() && value.back() == L'.') {
        value.pop_back();
    }
    return value;
}

bool ParseDurationText(const std::wstring& text, int& durationMinutes) {
    if (text.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    const double hours = std::wcstod(text.c_str(), &end);
    if (end == nullptr || *end != L'\0' || !std::isfinite(hours) ||
        hours <= 0.0 || hours >= 24.0) {
        return false;
    }
    try {
        durationMinutes = DurationMinutesFromHours(hours);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

void PopulateFields() {
    g.editEndInputMode = g.settings.endInputMode;
    g.editDeductLunch = g.settings.deductLunch;
    g.fields[0] = MakeField(FormatTimeValue(g.settings.startMinutes));
    g.fields[1] = MakeField(
        g.editEndInputMode == EndInputMode::WorkDuration
            ? FormatDurationHours(
                  EffectiveWorkMinutes(
                      g.settings.startMinutes, g.settings.endMinutes,
                      g.settings.lunchDurationMinutes, g.settings.deductLunch))
            : FormatTimeValue(g.settings.endMinutes));
    g.fields[2] = MakeField(FormatSalaryValue(g.settings.dailySalary));
    g.fields[3] = MakeField(g.settings.lunchDurationMinutes == 0
        ? L"" : std::to_wstring(g.settings.lunchDurationMinutes));
}

bool ParseTimeText(const std::wstring& text, int& minutes) {
    int hour = -1;
    int minute = -1;
    wchar_t trailing = L'\0';
    if (swscanf_s(text.c_str(), L"%d:%d %c", &hour, &minute, &trailing, 1) != 2 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    minutes = hour * 60 + minute;
    return true;
}

bool ReadLunchField(Settings& settings) {
    if (!ParseMinuteDurationText(g.fields[3].text, settings.lunchDurationMinutes)) return false;
    settings.deductLunch = g.editDeductLunch && settings.lunchDurationMinutes > 0;
    return true;
}

bool ReadFieldSettings(Settings& settings) {
    settings = g.settings;
    if (!ParseTimeText(g.fields[0].text, settings.startMinutes)) {
        return false;
    }

    if (!ReadLunchField(settings)) return false;
    settings.endInputMode = g.editEndInputMode;
    if (g.editEndInputMode == EndInputMode::WorkDuration) {
        int durationMinutes = 0;
        if (!ParseDurationText(g.fields[1].text, durationMinutes)) {
            return false;
        }
        try {
            settings.endMinutes = EndMinutesWithLunch(settings.startMinutes, durationMinutes,
                settings.lunchDurationMinutes, settings.deductLunch);
        } catch (const std::invalid_argument&) { return false; }
    } else if (!ParseTimeText(g.fields[1].text, settings.endMinutes) ||
               settings.startMinutes == settings.endMinutes) {
        return false;
    }

    try {
        (void)EffectiveWorkMinutes(settings.startMinutes, settings.endMinutes,
            settings.lunchDurationMinutes, settings.deductLunch);
    } catch (const std::invalid_argument&) { return false; }

    const std::wstring& salaryText = g.fields[2].text;
    if (salaryText.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    const double salary = std::wcstod(salaryText.c_str(), &end);
    if (end == nullptr || *end != L'\0' || !std::isfinite(salary) ||
        salary < 0.0 || salary > 1000000.0) {
        return false;
    }
    settings.dailySalary = salary;
    return true;
}

// -- clipboard --------------------------------------------------------------

std::wstring ReadClipboardText(HWND owner) {
    if (OpenClipboard(owner) == 0) {
        return {};
    }
    std::wstring text;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT); handle != nullptr) {
        if (const auto* data = static_cast<const wchar_t*>(GlobalLock(handle));
            data != nullptr) {
            text = data;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    // A pasted newline would otherwise be silently dropped mid-string.
    const std::size_t breakAt = text.find_first_of(L"\r\n");
    if (breakAt != std::wstring::npos) {
        text.resize(breakAt);
    }
    return text;
}

void WriteClipboardText(HWND owner, const std::wstring& text) {
    if (text.empty() || OpenClipboard(owner) == 0) {
        return;
    }
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes); handle != nullptr) {
        if (auto* data = static_cast<wchar_t*>(GlobalLock(handle)); data != nullptr) {
            memcpy(data, text.c_str(), bytes);
            GlobalUnlock(handle);
            SetClipboardData(CF_UNICODETEXT, handle);
        } else {
            GlobalFree(handle);
        }
    }
    CloseClipboard();
}

// -- actions ----------------------------------------------------------------

void ApplySave() {
    Settings parsed;
    if (!ReadFieldSettings(parsed)) {
        SetFooter(kValidationFooter, true);
        Present();
        return;
    }
    g.settings = NormalizeSettings(parsed);
    SaveSettings(g.settingsPath, g.settings);
    // A new schedule can put "now" after the end time; that is an edit, not a
    // shift ending, so rebase the reminder instead of letting it fire.
    ResetShiftReminder();
    SetExpanded(false);
}

void ToggleAutostart() {
    const bool target = !g.autostartEnabled;
    const AutostartResult result = SetAutostartEnabled(target);
    RefreshAutostart();
    if (!result.ok) {
        SetFooter(result.message, true);
    } else if (g.autostartEnabled != target) {
        SetFooter(L"开机自启未生效, 请检查系统设置", true);
    } else {
        ResetFooter();
    }
    Present();
}

void TogglePinned() {
    g.pinned = !g.pinned;
    SetWindowPos(
        g.window, g.pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Present();
}

void ToggleDeductLunch() {
    Settings draft = g.settings;
    if (!ReadLunchField(draft) || draft.lunchDurationMinutes == 0) {
        SetFooter(L"请先填写有效的午休时长(分钟)", true);
    } else {
        g.editDeductLunch = !g.editDeductLunch;
        ResetFooter();
    }
    Present();
}

void SetEndInputMode(EndInputMode mode) {
    if (g.editEndInputMode == mode) return;
    Settings draft;
    if (!ReadFieldSettings(draft)) {
        SetFooter(kValidationFooter, true);
        Present();
        return;
    }
    g.fields[1] = MakeField(mode == EndInputMode::WorkDuration
        ? FormatDurationHours(EffectiveWorkMinutes(draft.startMinutes, draft.endMinutes,
            draft.lunchDurationMinutes, draft.deductLunch))
        : FormatTimeValue(draft.endMinutes));
    g.editEndInputMode = mode;
    if (g.focusedField == 1) g.fields[1] = SelectAll(g.fields[1]);
    ResetFooter();
    Present();
}

void BeginScreenshotOcr(const std::wstring& path) {
    if (g.ocrTask.valid()) {
        g.trayIcon.ShowBalloon(L"截图识别", L"正在识别，请稍后重试。", true);
        return;
    }
    try {
        g.ocrTask = std::async(std::launch::async, [path] { return RecognizeScreenshot(path); });
    } catch (...) {
        g.trayIcon.ShowBalloon(L"截图识别", L"无法开始识别，请重试。", true);
    }
}

void PollScreenshotOcr() {
    if (!g.ocrTask.valid() || g.ocrTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    OcrResult result;
    try { result = g.ocrTask.get(); }
    catch (...) { result.error = L"识别失败，请重新拖入清晰的打卡截图。"; }
    if (result.minutes) {
        Settings updated = g.settings;
        try {
            if (updated.endInputMode == EndInputMode::WorkDuration) {
                const int duration = EffectiveWorkMinutes(updated.startMinutes, updated.endMinutes,
                    updated.lunchDurationMinutes, updated.deductLunch);
                updated.endMinutes = EndMinutesWithLunch(*result.minutes, duration,
                    updated.lunchDurationMinutes, updated.deductLunch);
            }
            updated.startMinutes = *result.minutes;
            if (NormalizeSettings(updated).startMinutes != updated.startMinutes ||
                updated.startMinutes == updated.endMinutes) throw std::invalid_argument("schedule");
            (void)EffectiveWorkMinutes(updated.startMinutes, updated.endMinutes,
                updated.lunchDurationMinutes, updated.deductLunch);
            g.settings = updated;
            SaveSettings(g.settingsPath, g.settings);
            PopulateFields();
            SetFocusedField(-1);
            ResetShiftReminder();
            ResetFooter();
            Present();
            return; // Success is silent: only the updated time is feedback.
        } catch (const std::invalid_argument&) {
            g.trayIcon.ShowBalloon(L"截图识别", L"时间与排班冲突，请调整设置。", true);
            return;
        }
    }
    g.trayIcon.ShowBalloon(L"截图识别", L"识别失败，请重试清晰截图。", true);
}

// -- keyboard ---------------------------------------------------------------

void FocusNextField(int direction) {
    if (!g.expanded) {
        return;
    }
    int index = g.focusedField;
    if (index < 0) {
        index = direction > 0 ? 0 : kFieldCount - 1;
    } else {
        index = (index + direction + kFieldCount) % kFieldCount;
    }
    SetFocusedField(index);
    g.fields[index] = SelectAll(g.fields[index]);
    Present();
}

bool HandleFieldKey(WPARAM key) {
    const int index = g.focusedField;
    if (index < 0 || index >= kFieldCount) {
        return false;
    }
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    FieldState& field = g.fields[index];

    switch (key) {
        case VK_LEFT:
            field = MoveCaret(field, -1, shift);
            break;
        case VK_RIGHT:
            field = MoveCaret(field, 1, shift);
            break;
        case VK_HOME:
            field = MoveToStart(field, shift);
            break;
        case VK_END:
            field = MoveToEnd(field, shift);
            break;
        case VK_BACK:
            field = DeleteBackward(field);
            break;
        case VK_DELETE:
            field = DeleteForward(field);
            break;
        case 'A':
            if (!control) {
                return false;
            }
            field = SelectAll(field);
            break;
        case 'C':
            if (!control) {
                return false;
            }
            WriteClipboardText(g.window, SelectedText(field));
            break;
        case 'X':
            if (!control) {
                return false;
            }
            WriteClipboardText(g.window, SelectedText(field));
            field = DeleteSelection(field);
            break;
        case 'V': {
            if (!control) {
                return false;
            }
            const std::wstring pasted = ReadClipboardText(g.window);
            field = InsertText(field, pasted, FilterFor(index), MaxLengthFor(index));
            break;
        }
        default:
            return false;
    }

    if (g.footerIsError) {
        ResetFooter();
    }
    g.caretVisible = true;
    SetTimer(g.window, kCaretTimer, kCaretBlinkMs, nullptr);
    Present();
    return true;
}

}  // namespace offwork
