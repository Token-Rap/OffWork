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
    CHECK(NormalizeSettings(invalid).endInputMode == EndInputMode::EndTime);

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

    std::filesystem::remove(path, error);
}

}  // namespace

int main() {
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

    Settings lunch;
    lunch.lunchStartMinutes = 720;
    lunch.lunchEndMinutes = 810;
    lunch.deductLunch = true;
    auto path = std::filesystem::temp_directory_path() / L"offwork-lunch-test.ini";
    offwork::SaveSettings(path.wstring(), lunch);
    auto loaded = offwork::LoadSettings(path.wstring());
    CHECK(loaded.lunchStartMinutes == 720 && loaded.lunchEndMinutes == 810);
    CHECK(loaded.deductLunch);
    // Remove the new keys to reproduce an actual legacy configuration.
    WritePrivateProfileStringW(L"work", L"lunchStartMinutes", nullptr, path.c_str());
    WritePrivateProfileStringW(L"work", L"lunchEndMinutes", nullptr, path.c_str());
    WritePrivateProfileStringW(L"work", L"deductLunch", nullptr, path.c_str());
    loaded = offwork::LoadSettings(path.wstring());
    CHECK(loaded.lunchStartMinutes == -1 && loaded.lunchEndMinutes == -1);
    CHECK(!loaded.deductLunch);
    lunch.lunchEndMinutes = -1;
    CHECK(!NormalizeSettings(lunch).deductLunch);
    lunch.lunchEndMinutes = 720;
    CHECK(NormalizeSettings(lunch).lunchStartMinutes == -1);
    lunch.lunchStartMinutes = 1410;
    lunch.lunchEndMinutes = 30;
    offwork::SaveSettings(path.wstring(), lunch);
    loaded = offwork::LoadSettings(path.wstring());
    CHECK(loaded.lunchStartMinutes == 1410 && loaded.lunchEndMinutes == 30);
    std::filesystem::remove(path);
    std::cout << "All OffWork settings tests passed.\n";
    return 0;
}
