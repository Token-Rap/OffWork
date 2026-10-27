#include "settings.h"

#include "check.h"
#include "theme.h"

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {

using offwork::NormalizeSettings;
using offwork::EndInputMode;
using offwork::Settings;

Settings WithScale(int percent) {
    Settings settings;
    settings.uiScalePercent = percent;
    return settings;
}

Settings WithOpacity(int percent) {
    Settings settings;
    settings.opacityPercent = percent;
    return settings;
}

void ScaleIsClampedToTheSupportedRange() {
    CHECK(NormalizeSettings(WithScale(10)).uiScalePercent == offwork::kMinScalePercent);
    CHECK(NormalizeSettings(WithScale(500)).uiScalePercent == offwork::kMaxScalePercent);
    CHECK(NormalizeSettings(WithScale(100)).uiScalePercent == 100);
    CHECK(NormalizeSettings(WithScale(75)).uiScalePercent == 75);
    CHECK(NormalizeSettings(WithScale(150)).uiScalePercent == 150);
}

void OpacityIsClampedToTheSliderRange() {
    CHECK(NormalizeSettings(WithOpacity(0)).opacityPercent == offwork::kMinOpacityPercent);
    CHECK(NormalizeSettings(WithOpacity(1000)).opacityPercent == 100);
    CHECK(NormalizeSettings(WithOpacity(50)).opacityPercent == 50);
}

void AnImpossibleWorkDayFallsBackToTheDefault() {
    Settings sameStartAndEnd;
    sameStartAndEnd.startMinutes = 9 * 60;
    sameStartAndEnd.endMinutes = 9 * 60;
    const Settings fixed = NormalizeSettings(sameStartAndEnd);
    CHECK(fixed.startMinutes == 9 * 60);
    CHECK(fixed.endMinutes == 18 * 60);

    Settings outOfRange;
    outOfRange.startMinutes = -5;
    outOfRange.endMinutes = 99999;
    const Settings repaired = NormalizeSettings(outOfRange);
    CHECK(repaired.startMinutes == 9 * 60);
    CHECK(repaired.endMinutes == 18 * 60);
}

void AValidOvernightShiftIsLeftAlone() {
    Settings overnight;
    overnight.startMinutes = 22 * 60;
    overnight.endMinutes = 6 * 60;
    const Settings kept = NormalizeSettings(overnight);
    CHECK(kept.startMinutes == 22 * 60);
    CHECK(kept.endMinutes == 6 * 60);
}

void SalaryIsClampedAndNonFiniteValuesAreRejected() {
    Settings negative;
    negative.dailySalary = -1.0;
    CHECK(NormalizeSettings(negative).dailySalary == 0.0);

    Settings huge;
    huge.dailySalary = 9e9;
    CHECK(NormalizeSettings(huge).dailySalary == 1000000.0);

    Settings notANumber;
    notANumber.dailySalary = std::nan("");
    CHECK(NormalizeSettings(notANumber).dailySalary == 0.0);

    Settings ordinary;
    ordinary.dailySalary = 500.0;
    CHECK(NormalizeSettings(ordinary).dailySalary == 500.0);
}

void NormalizeDoesNotTouchItsInput() {
    Settings original;
    original.uiScalePercent = 500;
    const Settings copy = NormalizeSettings(original);
    CHECK(original.uiScalePercent == 500);
    CHECK(copy.uiScalePercent == offwork::kMaxScalePercent);
}

void InvalidEndInputModeFallsBackWithoutChangingValidModes() {
    Settings invalid;
    invalid.endInputMode = static_cast<EndInputMode>(99);
    CHECK(NormalizeSettings(invalid).endInputMode == EndInputMode::WorkDuration);

    Settings fixed;
    fixed.endInputMode = EndInputMode::EndTime;
    CHECK(NormalizeSettings(fixed).endInputMode == EndInputMode::EndTime);

    Settings duration;
    duration.endInputMode = EndInputMode::WorkDuration;
    CHECK(NormalizeSettings(duration).endInputMode == EndInputMode::WorkDuration);
}

void EndInputModeRoundTripsThroughTheIniFile() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        L"offwork-settings-mode-test.ini";
    std::error_code error;
    std::filesystem::remove(path, error);

    Settings duration;
    duration.startMinutes = 9 * 60;
    duration.endMinutes = 17 * 60 + 30;
    duration.endInputMode = EndInputMode::WorkDuration;
    offwork::SaveSettings(path.wstring(), duration);

    const Settings loaded = offwork::LoadSettings(path.wstring());
    CHECK(loaded.startMinutes == duration.startMinutes);
    CHECK(loaded.endMinutes == duration.endMinutes);
    CHECK(loaded.endInputMode == EndInputMode::WorkDuration);

    duration.endInputMode = EndInputMode::EndTime;
    offwork::SaveSettings(path.wstring(), duration);
    CHECK(offwork::LoadSettings(path.wstring()).endInputMode == EndInputMode::EndTime);

    std::filesystem::remove(path, error);
}

void MissingOrInvalidEndModeDefaultsToDurationWithoutChangingTheSchedule() {
    CHECK(Settings{}.endInputMode == EndInputMode::WorkDuration);
    const auto path = std::filesystem::temp_directory_path() /
        (L"offwork-mode-default-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    CHECK(offwork::LoadSettings(path.wstring()).endInputMode == EndInputMode::WorkDuration);
    WritePrivateProfileStringW(L"work", L"startMinutes", L"532", path.c_str());
    WritePrivateProfileStringW(L"work", L"endMinutes", L"1130", path.c_str());
    for (const auto value : {L"", L"unknown", L"duration", L"DURATION", L"endTime", L"ENDTIME"}) {
        WritePrivateProfileStringW(L"work", L"endInputMode", value, path.c_str());
        const auto loaded = offwork::LoadSettings(path.wstring());
        const auto expected = _wcsicmp(value, L"endTime") == 0
            ? EndInputMode::EndTime : EndInputMode::WorkDuration;
        CHECK(loaded.endInputMode == expected);
        CHECK(loaded.startMinutes == 532 && loaded.endMinutes == 1130);
    }
    WritePrivateProfileStringW(L"work", L"endInputMode", nullptr, path.c_str());
    CHECK(offwork::LoadSettings(path.wstring()).endInputMode == EndInputMode::WorkDuration);
    std::filesystem::remove(path);
}

}  // namespace

int main() {
    MissingOrInvalidEndModeDefaultsToDurationWithoutChangingTheSchedule();
    {
        const auto maskPath = std::filesystem::temp_directory_path() /
            (L"offwork-mask-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        std::filesystem::remove(maskPath);
        CHECK(!offwork::LoadSettings(maskPath.wstring()).hideEarned);
        Settings masked;
        masked.hideEarned = true;
        offwork::SaveSettings(maskPath.wstring(), masked);
        CHECK(offwork::LoadSettings(maskPath.wstring()).hideEarned);
        masked.hideEarned = false;
        offwork::SaveSettings(maskPath.wstring(), masked);
        CHECK(!offwork::LoadSettings(maskPath.wstring()).hideEarned);
        WritePrivateProfileStringW(L"window", L"hideEarned", nullptr, maskPath.c_str());
        CHECK(!offwork::LoadSettings(maskPath.wstring()).hideEarned);
        std::filesystem::remove(maskPath);
    }
    ScaleIsClampedToTheSupportedRange();
    OpacityIsClampedToTheSliderRange();
    AnImpossibleWorkDayFallsBackToTheDefault();
    AValidOvernightShiftIsLeftAlone();
    SalaryIsClampedAndNonFiniteValuesAreRejected();
    NormalizeDoesNotTouchItsInput();
    InvalidEndInputModeFallsBackWithoutChangingValidModes();
    EndInputModeRoundTripsThroughTheIniFile();

    const auto path = std::filesystem::temp_directory_path() /
        (L"offwork-lunch-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    auto loaded = offwork::LoadSettings(path.wstring());
    CHECK(loaded.lunchStartMinutes == 720 && loaded.lunchEndMinutes == 810);
    CHECK(!loaded.deductLunch);
    // Duration-only settings migrate from noon, independently of the old checkbox.
    for (const auto deduct : {L"0", L"1"}) {
        WritePrivateProfileStringW(L"work", L"deductLunch", deduct, path.c_str());
        for (const int duration : {30, 60, 90, 180}) {
            WritePrivateProfileStringW(L"work", L"lunchDurationMinutes",
                std::to_wstring(duration).c_str(), path.c_str());
            loaded = offwork::LoadSettings(path.wstring());
            CHECK(loaded.lunchStartMinutes == 720);
            CHECK(loaded.lunchEndMinutes == 720 + duration);
            CHECK(loaded.startMinutes == 540 && loaded.endMinutes == 1080);
            CHECK(loaded.deductLunch == (deduct[0] == L'1'));
        }
    }
    for (const auto value : {L"0", L"", L"-1", L"1440", L"60oops", L"60.5", L"99999999999999999999", L"540"}) {
        WritePrivateProfileStringW(L"work", L"lunchDurationMinutes", value, path.c_str());
        loaded = offwork::LoadSettings(path.wstring());
        CHECK(loaded.lunchStartMinutes == -1 && loaded.lunchEndMinutes == -1);
    }
    // Explicit interval keys take precedence, even when invalid or incomplete.
    WritePrivateProfileStringW(L"work", L"lunchDurationMinutes", L"60", path.c_str());
    WritePrivateProfileStringW(L"work", L"lunchStartMinutes", L"690", path.c_str());
    WritePrivateProfileStringW(L"work", L"lunchEndMinutes", L"780", path.c_str());
    loaded = offwork::LoadSettings(path.wstring());
    CHECK(loaded.lunchStartMinutes == 690 && loaded.lunchEndMinutes == 780);
    offwork::SaveSettings(path.wstring(), loaded);
    CHECK(GetPrivateProfileIntW(L"work", L"lunchStartMinutes", -2, path.c_str()) == 690);
    CHECK(GetPrivateProfileIntW(L"work", L"lunchEndMinutes", -2, path.c_str()) == 780);
    wchar_t old[32] = {};
    CHECK(GetPrivateProfileStringW(L"work", L"lunchDurationMinutes", L"", old, 32, path.c_str()) == 0);
    CHECK(GetPrivateProfileIntW(L"work", L"deductLunch", -1, path.c_str()) == 1);
    CHECK(offwork::LoadSettings(path.wstring()).deductLunch);
    loaded.deductLunch = false;
    offwork::SaveSettings(path.wstring(), loaded);
    CHECK(GetPrivateProfileIntW(L"work", L"deductLunch", -1, path.c_str()) == 0);
    CHECK(!offwork::LoadSettings(path.wstring()).deductLunch);
    for (const auto value : {L"690", L"1440", L"-1", L"oops", L"780oops", L"99999999999999999999", L"", L"540", L"1081"}) {
        WritePrivateProfileStringW(L"work", L"lunchEndMinutes", value, path.c_str());
        loaded = offwork::LoadSettings(path.wstring());
        CHECK(loaded.lunchStartMinutes == -1 && loaded.lunchEndMinutes == -1);
    }
    WritePrivateProfileStringW(L"work", L"lunchEndMinutes", nullptr, path.c_str());
    CHECK(offwork::LoadSettings(path.wstring()).lunchStartMinutes == -1);
    // Disabled legacy lunches survive a save/reload without restoring defaults.
    offwork::SaveSettings(path.wstring(), loaded);
    CHECK(offwork::LoadSettings(path.wstring()).lunchEndMinutes == -1);
    Settings overnight;
    overnight.startMinutes = 1320;
    overnight.endMinutes = 360;
    overnight.lunchStartMinutes = 1410;
    overnight.lunchEndMinutes = 30;
    offwork::SaveSettings(path.wstring(), overnight);
    loaded = offwork::LoadSettings(path.wstring());
    CHECK(loaded.startMinutes == 1320 && loaded.endMinutes == 360);
    CHECK(loaded.lunchStartMinutes == 1410 && loaded.lunchEndMinutes == 30);
    std::filesystem::remove(path);
    std::cout << "All OffWork settings tests passed.\n";
    return 0;
}
