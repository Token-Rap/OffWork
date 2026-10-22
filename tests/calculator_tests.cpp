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

    // Lunch is paid: earnings and progress use the entire scheduled shift.
    const auto lunch = CalculateWorkday(12 * 3600, 540, 1080, 800, 60, true);
    CHECK(lunch.remainingSeconds == 6 * 3600);
    CHECK(Near(lunch.progress, 1.0 / 3.0));
    CHECK(Near(lunch.earned, 800.0 / 3.0));
    const auto halfway = CalculateWorkday(14 * 3600, 540, 1080, 800, 60, true);
    CHECK(halfway.remainingSeconds == 4 * 3600);
    CHECK(Near(halfway.progress, 5.0 / 9.0));
    CHECK(Near(halfway.earned, 800.0 * 5.0 / 9.0));
    CHECK(Near(CalculateWorkday(9 * 3600 + 30 * 60, 540, 1080, 800, 60, true).progress, 1.0 / 18.0));
    CHECK(Near(CalculateWorkday(10 * 3600, 540, 1080, 800, 60, true).progress, 1.0 / 9.0));
    CHECK(Near(CalculateWorkday(8 * 3600, 540, 1080, 800, 60, true).earned, 0));
    CHECK(CalculateWorkday(8 * 3600, 540, 1080, 800, 60, true).remainingSeconds == 3600);
    CHECK(Near(CalculateWorkday(18 * 3600, 540, 1080, 800, 60, true).earned, 800));
    CHECK(Near(CalculateWorkday(13 * 3600, 540, 1080, 900, 60, false).earned, 400));
    CHECK(Near(CalculateWorkday(13 * 3600, 540, 1080, 900, 0, true).earned, 400));
    // Lunch never changes the scheduled end or the real remaining time.
    const int deductedEnd = offwork::EndMinutesWithLunch(540, 480, 60, true);
    CHECK(deductedEnd == 1020);
    const auto deducted = CalculateWorkday(13 * 3600, 540, deductedEnd, 700, 60, true);
    const auto continuous = CalculateWorkday(13 * 3600, 540, 1020, 700, 60, false);
    CHECK(deducted.remainingSeconds == 4 * 3600);
    CHECK(deducted.remainingSeconds == continuous.remainingSeconds);
    CHECK(Near(deducted.progress, 0.5));
    CHECK(Near(deducted.earned, 350));
    CHECK(Near(continuous.earned, 350));
    CHECK(Near(CalculateWorkday(17 * 3600, 540, deductedEnd, 700, 60, true).earned, 700));
    CHECK(offwork::EndMinutesWithLunch(540, 120, 60, true) == 660);
    CHECK(offwork::EndMinutesWithLunch(540, 480, 60, false) == 1020);
    CHECK(offwork::EndMinutesWithLunch(540, 480, 0, true) == 1020);
    CHECK(offwork::EndMinutesWithLunch(1320, 480, 60, true) == 360);
    const auto night = CalculateWorkday(0, 1320, 360, 700, 60, true);
    CHECK(night.remainingSeconds == 360 * 60);
    CHECK(Near(night.earned, 175));
    CHECK(Near(CalculateWorkday(23 * 3600, 1320, 360, 700, 60, true).earned, 87.5));
    CHECK(offwork::EffectiveWorkMinutes(540, 780, 60, true) == 180);
    for (const int lunchDuration : {60, 90, -1, 1440}) {
        bool noWork = false;
        try { (void)CalculateWorkday(12*3600, 720, 780, 500, lunchDuration, true); }
        catch (const std::invalid_argument&) { noWork = true; }
        CHECK(noWork);
    }
    for (const int lunchDuration : {480, 481, -1, 1440}) {
        bool invalidLunch = false;
        try { (void)offwork::EndMinutesWithLunch(540, 480, lunchDuration, true); }
        catch (const std::invalid_argument&) { invalidLunch = true; }
        CHECK(invalidLunch);
    }
    CHECK(offwork::EndMinutesWithLunch(540, 1380, 60, true) == 480);
    CHECK(offwork::EndMinutesWithLunch(540, 1439, 60, true) == 539);

    // Compare every second, including pre-work, noon, the zero-display tail,
    // real shift end and overnight rollover. The checkbox changes only display.
    for (const int start : {540, 1320}) {
        const int end = EndMinutesFromDuration(start, 480);
        for (const int lunchMinutes : {0, 60, 90}) {
            for (int now = 0; now < 24 * 3600; ++now) {
                const auto paid = CalculateWorkday(now, start, end, 800, lunchMinutes, false);
                const auto checked = CalculateWorkday(now, start, end, 800, lunchMinutes, true);
                CHECK(checked.phase == paid.phase);
                CHECK(checked.remainingSeconds == paid.remainingSeconds);
                CHECK(Near(checked.earned, paid.earned));
                CHECK(Near(checked.progress, paid.progress));
                CHECK(paid.countdownSeconds == paid.remainingSeconds);
                const int expected = paid.phase == WorkdayPhase::Working
                    ? std::max(0, paid.remainingSeconds - lunchMinutes * 60)
                    : paid.remainingSeconds;
                CHECK(checked.countdownSeconds == expected);
            }
        }
    }
    const auto zeroCountdown = CalculateWorkday(17 * 3600, 540, 1080, 900, 60, true);
    CHECK(zeroCountdown.countdownSeconds == 0);
    CHECK(zeroCountdown.phase == WorkdayPhase::Working);
    CHECK(Near(zeroCountdown.earned, 800));
    // The paid hourly rate stays 900 / 9 = 100, including the lunch deduction.
    CHECK(Near(CalculateWorkday(13 * 3600, 540, 1080, 900, 60, true).earned -
               CalculateWorkday(12 * 3600, 540, 1080, 900, 60, true).earned, 100));
    std::cout << "All OffWork calculation tests passed.\n";
    return 0;
}
