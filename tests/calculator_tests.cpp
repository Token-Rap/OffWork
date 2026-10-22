#include "calculator.h"

#include "check.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

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

    // 09:00-18:00, lunch 12:00-13:30: 7.5 paid hours at 100/hour.
    const auto atLunch = CalculateWorkday(12 * 3600, 540, 1080, 750, 720, 810);
    CHECK(atLunch.onLunch);
    CHECK(Near(atLunch.earned, 300));
    const auto inLunch = CalculateWorkday(13 * 3600, 540, 1080, 750, 720, 810);
    CHECK(Near(inLunch.earned, 300));
    CHECK(Near(inLunch.progress, 0.4));
    CHECK(inLunch.remainingSeconds == 5 * 3600);
    const auto lunchEnd = CalculateWorkday(13 * 3600 + 30 * 60, 540, 1080, 750, 720, 810);
    CHECK(!lunchEnd.onLunch);
    CHECK(Near(lunchEnd.earned, 300));
    CHECK(Near(CalculateWorkday(14 * 3600, 540, 1080, 750, 720, 810).earned, 350));
    CHECK(Near(CalculateWorkday(18 * 3600, 540, 1080, 750, 720, 810).earned, 750));
    // Every lunch second is frozen, while the real deadline still advances.
    for (int now = 12 * 3600; now < 13 * 3600 + 30 * 60; ++now) {
        const auto frozen = CalculateWorkday(now, 540, 1080, 750, 720, 810);
        CHECK(frozen.phase == WorkdayPhase::Working && frozen.onLunch);
        CHECK(Near(frozen.earned, 300) && Near(frozen.progress, 0.4));
        CHECK(frozen.remainingSeconds == 18 * 3600 - now);
    }
    CHECK(Near(CalculateWorkday(12 * 3600 - 1, 540, 1080, 750, 720, 810).earned, 300 - 100.0 / 3600));
    CHECK(Near(CalculateWorkday(13 * 3600 + 30 * 60 + 1, 540, 1080, 750, 720, 810).earned, 300 + 100.0 / 3600));
    CHECK(!CalculateWorkday(12 * 3600 - 1, 540, 1080, 750, 720, 810).onLunch);
    CHECK(Near(CalculateWorkday(8 * 3600, 540, 1080, 750, 720, 810).earned, 0));
    CHECK(CalculateWorkday(8 * 3600, 540, 1080, 750, 720, 810).remainingSeconds == 3600);
    CHECK(!CalculateWorkday(18 * 3600, 540, 1080, 750, 720, 810).onLunch);
    // A restart in the lunch window derives the same frozen value, even at midnight.
    const auto nightLunch = CalculateWorkday(0, 1320, 360, 700, 1410, 30);
    CHECK(nightLunch.onLunch && Near(nightLunch.earned, 150));
    CHECK(nightLunch.remainingSeconds == 6 * 3600);
    CHECK(Near(CalculateWorkday(30 * 60, 1320, 360, 700, 1410, 30).earned, 150));
    CHECK(!CalculateWorkday(30 * 60, 1320, 360, 700, 1410, 30).onLunch);
    CHECK(Near(CalculateWorkday(3600, 1320, 360, 700, 1410, 30).earned, 200));
    CHECK(Near(CalculateWorkday(6 * 3600, 1320, 360, 700, 1410, 30).earned, 700));
    // Lunch can touch either shift boundary but cannot consume all work time.
    CHECK(CalculateWorkday(9 * 3600, 540, 1080, 800, 540, 600).onLunch);
    CHECK(CalculateWorkday(17 * 3600, 540, 1080, 800, 1020, 1080).onLunch);
    CHECK(Near(CalculateWorkday(17 * 3600, 540, 1080, 800, 1020, 1080).earned, 800));
    for (const auto interval : {std::pair{720, 720}, {780, 720}, {500, 600},
                               {1020, 1081}, {540, 1080}, {-1, 780}, {720, 1440}}) {
        bool invalidLunch = false;
        try { (void)CalculateWorkday(12 * 3600, 540, 1080, 750, interval.first, interval.second); }
        catch (const std::invalid_argument&) { invalidLunch = true; }
        CHECK(invalidLunch);
    }
    std::cout << "All OffWork calculation tests passed.\n";
    return 0;
}
