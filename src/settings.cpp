#include "settings.h"

#include "theme.h"
#include "calculator.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <iterator>

namespace offwork {
namespace {

constexpr int kMinutesPerDay = 24 * 60;
constexpr double kMaxDailySalary = 1000000.0;

// Returns presence separately from validity: an empty/invalid interval key must
// not resurrect a legacy duration. Strict parsing also rejects truncated numbers.
bool ReadMinuteSetting(const std::wstring& path, const wchar_t* key, int& minutes) {
    wchar_t text[64] = {};
    const DWORD length = GetPrivateProfileStringW(
        L"work", key, L"missing", text, static_cast<DWORD>(std::size(text)), path.c_str());
    if (wcscmp(text, L"missing") == 0 &&
        GetPrivateProfileStringW(L"work", key, L"", text,
            static_cast<DWORD>(std::size(text)), path.c_str()) == 0) return false;
    minutes = -1;
    if (length == 0 || length >= std::size(text) - 1) return true;
    int value = 0;
    for (const wchar_t digit : std::wstring(text)) {
        if (digit < L'0' || digit > L'9') return true;
        value = value * 10 + digit - L'0';
        if (value >= kMinutesPerDay) return true;
    }
    minutes = value;
    return true;
}

}  // namespace

Settings NormalizeSettings(const Settings& settings) {
    Settings normalized = settings;

    if (settings.endInputMode != EndInputMode::EndTime &&
        settings.endInputMode != EndInputMode::WorkDuration) {
        normalized.endInputMode = EndInputMode::WorkDuration;
    }

    normalized.uiScalePercent =
        std::clamp(settings.uiScalePercent, kMinScalePercent, kMaxScalePercent);
    normalized.opacityPercent =
        std::clamp(settings.opacityPercent, kMinOpacityPercent, 100);

    if (!std::isfinite(settings.dailySalary) || settings.dailySalary < 0.0) {
        normalized.dailySalary = 0.0;
    } else {
        normalized.dailySalary = std::min(settings.dailySalary, kMaxDailySalary);
    }

    const bool startValid =
        settings.startMinutes >= 0 && settings.startMinutes < kMinutesPerDay;
    const bool endValid =
        settings.endMinutes >= 0 && settings.endMinutes < kMinutesPerDay;
    if (!startValid || !endValid || settings.startMinutes == settings.endMinutes) {
        normalized.startMinutes = 9 * 60;
        normalized.endMinutes = 18 * 60;
    }

    try {
        (void)LunchDurationMinutes(normalized.startMinutes, normalized.endMinutes,
            normalized.lunchStartMinutes, normalized.lunchEndMinutes);
    } catch (const std::invalid_argument&) {
        normalized.lunchStartMinutes = -1;
        normalized.lunchEndMinutes = -1;
    }
    return normalized;
}

std::wstring SettingsFilePath() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
        return L"OffWork.ini";
    }

    std::filesystem::path directory(localAppData);
    CoTaskMemFree(localAppData);
    directory /= L"OffWork";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return (directory / L"settings.ini").wstring();
}

Settings LoadSettings(const std::wstring& path) {
    Settings settings;
    settings.hideEarned = GetPrivateProfileIntW(L"window", L"hideEarned", 0, path.c_str()) == 1;
    settings.startMinutes = GetPrivateProfileIntW(
        L"work", L"startMinutes", settings.startMinutes, path.c_str());
    settings.endMinutes = GetPrivateProfileIntW(
        L"work", L"endMinutes", settings.endMinutes, path.c_str());
    settings.uiScalePercent = static_cast<int>(GetPrivateProfileIntW(
        L"window", L"scalePercent", settings.uiScalePercent, path.c_str()));
    settings.opacityPercent = static_cast<int>(GetPrivateProfileIntW(
        L"window", L"opacityPercent", settings.opacityPercent, path.c_str()));

    int lunchStart = -1;
    int lunchEnd = -1;
    const bool hasStart = ReadMinuteSetting(path, L"lunchStartMinutes", lunchStart);
    const bool hasEnd = ReadMinuteSetting(path, L"lunchEndMinutes", lunchEnd);
    if (hasStart || hasEnd) {
        settings.lunchStartMinutes = lunchStart;
        settings.lunchEndMinutes = lunchEnd;
    } else {
        int legacyDuration = 0;
        if (ReadMinuteSetting(path, L"lunchDurationMinutes", legacyDuration)) {
            settings.lunchStartMinutes = legacyDuration > 0 ? 12 * 60 : -1;
            settings.lunchEndMinutes = legacyDuration > 0
                ? (12 * 60 + legacyDuration) % kMinutesPerDay : -1;
        }
    }

    wchar_t inputMode[32] = {};
    GetPrivateProfileStringW(
        L"work", L"endInputMode", L"duration", inputMode,
        static_cast<DWORD>(std::size(inputMode)), path.c_str());
    settings.endInputMode = _wcsicmp(inputMode, L"endTime") == 0
        ? EndInputMode::EndTime
        : EndInputMode::WorkDuration;

    wchar_t salary[64] = {};
    GetPrivateProfileStringW(
        L"work", L"dailySalary", L"500", salary,
        static_cast<DWORD>(std::size(salary)), path.c_str());
    wchar_t* end = nullptr;
    const double parsedSalary = std::wcstod(salary, &end);
    if (end != salary && std::isfinite(parsedSalary) && parsedSalary >= 0.0) {
        settings.dailySalary = parsedSalary;
    }

    return NormalizeSettings(settings);
}

void SaveSettings(const std::wstring& path, const Settings& settings) {
    const Settings normalized = NormalizeSettings(settings);
    WritePrivateProfileStringW(L"window", L"hideEarned", normalized.hideEarned ? L"1" : L"0", path.c_str());
    wchar_t value[64] = {};

    swprintf_s(value, L"%d", normalized.startMinutes);
    WritePrivateProfileStringW(L"work", L"startMinutes", value, path.c_str());
    swprintf_s(value, L"%d", normalized.endMinutes);
    WritePrivateProfileStringW(L"work", L"endMinutes", value, path.c_str());
    WritePrivateProfileStringW(
        L"work", L"endInputMode",
        normalized.endInputMode == EndInputMode::WorkDuration
            ? L"duration"
            : L"endTime",
        path.c_str());
    swprintf_s(value, L"%d", normalized.lunchStartMinutes);
    WritePrivateProfileStringW(L"work", L"lunchStartMinutes", value, path.c_str());
    swprintf_s(value, L"%d", normalized.lunchEndMinutes);
    WritePrivateProfileStringW(L"work", L"lunchEndMinutes", value, path.c_str());
    WritePrivateProfileStringW(L"work", L"lunchDurationMinutes", nullptr, path.c_str());
    WritePrivateProfileStringW(L"work", L"deductLunch", nullptr, path.c_str());
    swprintf_s(value, L"%.2f", normalized.dailySalary);
    WritePrivateProfileStringW(L"work", L"dailySalary", value, path.c_str());
    swprintf_s(value, L"%d", normalized.uiScalePercent);
    WritePrivateProfileStringW(L"window", L"scalePercent", value, path.c_str());
    swprintf_s(value, L"%d", normalized.opacityPercent);
    WritePrivateProfileStringW(L"window", L"opacityPercent", value, path.c_str());
}

}  // namespace offwork
