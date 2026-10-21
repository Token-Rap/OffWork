#include "calculator.h"

#include "check.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

bool Near(double left, double right, double epsilon = 0.0001) {
    return std::abs(left - right) < epsilon;
}

}  // namespace

int main() {
    using offwork::CalculateWorkday;
    using offwork::DurationMinutesFromHours;
    using offwork::EndMinutesFromDuration;
    using offwork::WorkDurationMinutes;
    using offwork::WorkdayPhase;

    CHECK(EndMinutesFromDuration(9 * 60, 8 * 60) == 17 * 60);
    CHECK(EndMinutesFromDuration(9 * 60, 9 * 60) == 18 * 60);
    CHECK(EndMinutesFromDuration(22 * 60, 8 * 60) == 6 * 60);
    CHECK(WorkDurationMinutes(9 * 60, 17 * 60) == 8 * 60);
    CHECK(WorkDurationMinutes(22 * 60, 6 * 60) == 8 * 60);
    CHECK(DurationMinutesFromHours(8.0) == 8 * 60);
    CHECK(DurationMinutesFromHours(8.5) == 8 * 60 + 30);
    CHECK(DurationMinutesFromHours(9.0) == 9 * 60);

    bool invalidDuration = false;
    try {
        (void)EndMinutesFromDuration(9 * 60, 24 * 60);
    } catch (const std::invalid_argument&) {
        invalidDuration = true;
    }
    CHECK(invalidDuration);

    bool invalidDurationHours = false;
    try {
        (void)DurationMinutesFromHours(24.0);
    } catch (const std::invalid_argument&) {
        invalidDurationHours = true;
    }
    CHECK(invalidDurationHours);

    const auto working = CalculateWorkday(13 * 3600 + 30 * 60, 9 * 60, 18 * 60, 900.0);
    CHECK(working.phase == WorkdayPhase::Working);
    CHECK(working.remainingSeconds == 4 * 3600 + 30 * 60);
    CHECK(Near(working.earned, 450.0));
    CHECK(Near(working.progress, 0.5));

    const auto before = CalculateWorkday(8 * 3600, 9 * 60, 18 * 60, 600.0);
    CHECK(before.phase == WorkdayPhase::BeforeWork);
    CHECK(before.remainingSeconds == 3600);
    CHECK(Near(before.earned, 0.0));

    const auto after = CalculateWorkday(20 * 3600, 9 * 60, 18 * 60, 800.0);
    CHECK(after.phase == WorkdayPhase::AfterWork);
    CHECK(after.remainingSeconds == 0);
    CHECK(Near(after.earned, 800.0));

    const auto overnight = CalculateWorkday(1 * 3600, 22 * 60, 6 * 60, 800.0);
    CHECK(overnight.phase == WorkdayPhase::Working);
    CHECK(overnight.remainingSeconds == 5 * 3600);
    CHECK(Near(overnight.earned, 300.0));

    bool invalidHours = false;
    try {
        (void)CalculateWorkday(12 * 3600, 9 * 60, 9 * 60, 500.0);
    } catch (const std::invalid_argument&) {
        invalidHours = true;
    }
    CHECK(invalidHours);

    bool invalidSalary = false;
    try {
        (void)CalculateWorkday(12 * 3600, 9 * 60, 18 * 60, -1.0);
    } catch (const std::out_of_range&) {
        invalidSalary = true;
    }
    CHECK(invalidSalary);

    // Countdown is real time to end; effective work is credited from the end backwards.
    const auto lunch = CalculateWorkday(12 * 3600, 540, 1080, 800, 60, true);
    CHECK(lunch.remainingSeconds == 6 * 3600);
    CHECK(Near(lunch.progress, 0.25));
    CHECK(Near(lunch.earned, 200));
    const auto halfway = CalculateWorkday(14 * 3600, 540, 1080, 800, 60, true);
    CHECK(halfway.remainingSeconds == 4 * 3600);
    CHECK(Near(halfway.progress, 0.5));
    CHECK(Near(halfway.earned, 400));
    CHECK(Near(CalculateWorkday(9 * 3600 + 30 * 60, 540, 1080, 800, 60, true).progress, 0));
    CHECK(Near(CalculateWorkday(10 * 3600, 540, 1080, 800, 60, true).progress, 0));
    CHECK(Near(CalculateWorkday(8 * 3600, 540, 1080, 800, 60, true).earned, 0));
    CHECK(CalculateWorkday(8 * 3600, 540, 1080, 800, 60, true).remainingSeconds == 3600);
    CHECK(Near(CalculateWorkday(18 * 3600, 540, 1080, 800, 60, true).earned, 800));
    CHECK(Near(CalculateWorkday(13 * 3600, 540, 1080, 900, 60, false).earned, 400));
    CHECK(Near(CalculateWorkday(13 * 3600, 540, 1080, 900, 0, true).earned, 400));
    CHECK(offwork::EndMinutesWithLunch(540, 480, 60, true) == 1080);
    CHECK(offwork::EndMinutesWithLunch(540, 120, 60, true) == 720);
    CHECK(offwork::EndMinutesWithLunch(540, 480, 60, false) == 1020);
    CHECK(offwork::EndMinutesWithLunch(540, 480, 0, true) == 1020);
    CHECK(offwork::EndMinutesWithLunch(1320, 480, 60, true) == 420);
    const auto night = CalculateWorkday(0, 1320, 360, 700, 60, true);
    CHECK(night.remainingSeconds == 360 * 60);
    CHECK(Near(night.earned, 100));
    CHECK(Near(CalculateWorkday(23 * 3600, 1320, 360, 700, 60, true).earned, 0));
    CHECK(offwork::EffectiveWorkMinutes(540, 780, 60, true) == 180);
    for (const int lunchDuration : {60, 90, -1, 1440}) {
        bool noWork = false;
        try { (void)CalculateWorkday(12*3600, 720, 780, 500, lunchDuration, true); }
        catch (const std::invalid_argument&) { noWork = true; }
        CHECK(noWork);
    }
    bool tooLong = false;
    try { (void)offwork::EndMinutesWithLunch(540, 1380, 60, true); }
    catch (const std::invalid_argument&) { tooLong = true; }
    CHECK(tooLong);
    CHECK(offwork::EndMinutesWithLunch(540, 1380, 59, true) == 539);
    std::cout << "All OffWork calculation tests passed.\n";
    return 0;
}
