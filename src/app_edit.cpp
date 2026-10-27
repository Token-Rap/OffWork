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
    return fieldIndex == 2 ||
            (fieldIndex == 1 && g.editEndInputMode == EndInputMode::WorkDuration)
        ? FieldFilter::Decimal
        : FieldFilter::Time;
}

std::size_t MaxLengthFor(int fieldIndex) {
    return fieldIndex == 2 ? kSalaryMaxLength : kTimeMaxLength;
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
    g.fields[0] = MakeField(FormatTimeValue(g.settings.startMinutes));
    g.fields[1] = MakeField(
        g.editEndInputMode == EndInputMode::WorkDuration
            ? FormatDurationHours(
                  WorkDurationMinutes(g.settings.startMinutes, g.settings.endMinutes))
            : FormatTimeValue(g.settings.endMinutes));
    g.fields[2] = MakeField(FormatSalaryValue(g.settings.dailySalary));
    g.fields[3] = MakeField(g.settings.lunchStartMinutes < 0 ? L"" : FormatTimeValue(g.settings.lunchStartMinutes));
    g.fields[4] = MakeField(g.settings.lunchEndMinutes < 0 ? L"" : FormatTimeValue(g.settings.lunchEndMinutes));
}

bool ParseTimeText(const std::wstring& text, int& minutes) {
    const auto colon = text.find(L':');
    if (colon == std::wstring::npos || colon < 1 || colon > 2 ||
        text.size() <= colon + 1 || text.size() > colon + 3) return false;
    int hour = 0;
    int minute = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == colon) continue;
        if (text[i] < L'0' || text[i] > L'9') return false;
        int& value = i < colon ? hour : minute;
        value = value * 10 + text[i] - L'0';
    }
    if (hour > 23 || minute > 59) return false;
    minutes = hour * 60 + minute;
    return true;
}

namespace {

bool ParseSalaryText(const std::wstring& text, double& salary) {
    if (text.empty()) return false;
    wchar_t* end = nullptr;
    const double value = std::wcstod(text.c_str(), &end);
    if (end == text.c_str() || *end != L'\0' || !std::isfinite(value) ||
        value < 0.0 || value > 1000000.0) return false;
    // Match the cents shown in the editor and written to settings.ini.
    salary = std::round(value * 100.0) / 100.0;
    return true;
}

bool ValidWorkSettings(const Settings& settings) {
    try {
        (void)EffectiveWorkMinutes(settings.startMinutes, settings.endMinutes,
            settings.lunchStartMinutes, settings.lunchEndMinutes);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

FieldState SettingsField(int index) {
    switch (index) {
        case 0: return MakeField(FormatTimeValue(g.settings.startMinutes));
        case 1: return MakeField(g.settings.endInputMode == EndInputMode::WorkDuration
            ? FormatDurationHours(WorkDurationMinutes(g.settings.startMinutes, g.settings.endMinutes))
            : FormatTimeValue(g.settings.endMinutes));
        case 2: return MakeField(FormatSalaryValue(g.settings.dailySalary));
        case 3: return MakeField(g.settings.lunchStartMinutes < 0 ? L"" : FormatTimeValue(g.settings.lunchStartMinutes));
        default: return MakeField(g.settings.lunchEndMinutes < 0 ? L"" : FormatTimeValue(g.settings.lunchEndMinutes));
    }
}

void PersistLiveSettings(const Settings& settings) {
    g.settings = settings;
    g.editEndInputMode = settings.endInputMode;
    SaveSettings(g.settingsPath, g.settings);
    // An edit can put now past the end time; it must not fire a shift reminder.
    ResetShiftReminder();
    ResetFooter();
}

}  // namespace

bool ParseLunchTime(const std::wstring& text, int& minutes) {
    return text.size() == 5 && text[2] == L':' && ParseTimeText(text, minutes);
}

bool ReadLunchField(Settings& settings) {
    if (g.fields[3].text.empty() && g.fields[4].text.empty()) {
        settings.lunchStartMinutes = settings.lunchEndMinutes = -1;
        return true;
    }
    return ParseLunchTime(g.fields[3].text, settings.lunchStartMinutes) &&
        ParseLunchTime(g.fields[4].text, settings.lunchEndMinutes);
}

void ApplyLiveLunchChange() {
    Settings draft = g.settings;
    if (!ReadLunchField(draft) || !ValidWorkSettings(draft)) return;
    if (draft.lunchStartMinutes != g.settings.lunchStartMinutes ||
        draft.lunchEndMinutes != g.settings.lunchEndMinutes) PersistLiveSettings(draft);
}

void ApplyLiveEndChange() {
    Settings draft = g.settings;
    const auto& text = g.fields[1].text;
    if (draft.endInputMode == EndInputMode::WorkDuration) {
        int duration = 0;
        if (text.empty() || text.back() == L'.' || !ParseDurationText(text, duration)) return;
        draft.endMinutes = EndMinutesFromDuration(draft.startMinutes, duration);
    } else if (!ParseTimeText(text, draft.endMinutes) || text.size() - text.find(L':') != 3) {
        return; // Keep incomplete input editable without changing the schedule.
    }
    if (draft.endMinutes != g.settings.endMinutes && ValidWorkSettings(draft))
        PersistLiveSettings(draft);
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
            settings.endMinutes = EndMinutesFromDuration(settings.startMinutes, durationMinutes);
        } catch (const std::invalid_argument&) { return false; }
    } else if (!ParseTimeText(g.fields[1].text, settings.endMinutes) ||
               settings.startMinutes == settings.endMinutes) {
        return false;
    }

    return ValidWorkSettings(settings) && ParseSalaryText(g.fields[2].text, settings.dailySalary);
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

void ApplyFieldChange(int index) {
    if (index < 0 || index >= kFieldCount) return;
    const int otherLunchField = index == 3 ? 4 : 3;
    if (g.fields[index].text == SettingsField(index).text &&
        (index < 3 || g.fields[otherLunchField].text == SettingsField(otherLunchField).text)) return;
    Settings draft = g.settings;
    const auto& text = g.fields[index].text;
    bool valid = false;
    switch (index) {
        case 0:
            valid = ParseTimeText(text, draft.startMinutes);
            if (valid && draft.endInputMode == EndInputMode::WorkDuration) {
                draft.endMinutes = EndMinutesFromDuration(draft.startMinutes,
                    WorkDurationMinutes(g.settings.startMinutes, g.settings.endMinutes));
            }
            break;
        case 1:
            if (draft.endInputMode == EndInputMode::WorkDuration) {
                int duration = 0;
                valid = ParseDurationText(text, duration);
                if (valid) draft.endMinutes = EndMinutesFromDuration(draft.startMinutes, duration);
            } else {
                valid = ParseTimeText(text, draft.endMinutes);
            }
            break;
        case 2:
            valid = ParseSalaryText(text, draft.dailySalary);
            break;
        case 3:
        case 4:
            valid = ReadLunchField(draft);
            break;
    }
    if (valid && ValidWorkSettings(draft)) {
        PersistLiveSettings(draft);
    } else {
        SetFooter(kValidationFooter, true);
    }
    g.fields[index] = SettingsField(index);
    if (index >= 3) g.fields[otherLunchField] = SettingsField(otherLunchField);
    if (index == 0 && g.settings.endInputMode == EndInputMode::WorkDuration)
        g.fields[1] = SettingsField(1);
    Present();
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

void SetEndInputMode(EndInputMode mode) {
    if (g.editEndInputMode == mode) return;
    Settings draft;
    if (!ReadFieldSettings(draft)) {
        SetFooter(kValidationFooter, true);
        Present();
        return;
    }
    g.fields[1] = MakeField(mode == EndInputMode::WorkDuration
        ? FormatDurationHours(WorkDurationMinutes(draft.startMinutes, draft.endMinutes))
        : FormatTimeValue(draft.endMinutes));
    draft.endInputMode = mode;
    PersistLiveSettings(draft);
    if (g.focusedField == 1) g.fields[1] = SelectAll(g.fields[1]);
    ResetFooter();
    Present();
}

namespace {
void ShowScreenshotOcrError(const std::wstring& message) {
    // Windows may suppress subtle tray balloons (quiet time / notifications
    // disabled). Keep the actionable reason visible in the widget as well.
    SetExpanded(true);
    SetFooter(message, true);
    g.trayIcon.ShowBalloon(L"截图识别", message.c_str(), true);
    Present();
}
}  // namespace

void BeginScreenshotOcr(const std::wstring& path) {
    if (g.ocrTask.valid()) {
        ShowScreenshotOcrError(L"正在识别，请稍后重试。");
        return;
    }
    try {
        g.ocrTask = std::async(std::launch::async, [path] { return RecognizeScreenshot(path); });
    } catch (...) {
        ShowScreenshotOcrError(L"无法开始识别，请重试。");
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
                const int duration = WorkDurationMinutes(updated.startMinutes, updated.endMinutes);
                updated.endMinutes = EndMinutesFromDuration(*result.minutes, duration);
            }
            updated.startMinutes = *result.minutes;
            if (NormalizeSettings(updated).startMinutes != updated.startMinutes ||
                updated.startMinutes == updated.endMinutes) throw std::invalid_argument("schedule");
            (void)EffectiveWorkMinutes(updated.startMinutes, updated.endMinutes,
                updated.lunchStartMinutes, updated.lunchEndMinutes);
            g.settings = updated;
            SaveSettings(g.settingsPath, g.settings);
            PopulateFields();
            SetFocusedField(-1);
            ResetShiftReminder();
            ResetFooter();
            Present();
            return; // Success is silent: only the updated time is feedback.
        } catch (const std::invalid_argument&) {
            ShowScreenshotOcrError(L"时间与排班冲突，请调整设置。");
            return;
        }
    }
    ShowScreenshotOcrError(result.error.empty() ? L"识别失败，请重试清晰截图。" : result.error);
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
    if (index == 1) ApplyLiveEndChange();
    if (index >= 3) ApplyLiveLunchChange();
    g.caretVisible = true;
    SetTimer(g.window, kCaretTimer, kCaretBlinkMs, nullptr);
    Present();
    return true;
}

}  // namespace offwork
