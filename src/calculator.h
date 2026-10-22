#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace offwork {

enum class WorkdayPhase {
    BeforeWork,
    Working,
    AfterWork,
};

struct WorkdaySnapshot {
    WorkdayPhase phase;
    // Real time to the phase boundary; reminders must use this value.
    int remainingSeconds;
    double earned;
    double progress;
    int countdownSeconds = 0;
};

inline int WorkDurationMinutes(int startMinutes, int endMinutes) {
    constexpr int minutesPerDay = 24 * 60;
    if (startMinutes < 0 || startMinutes >= minutesPerDay ||
        endMinutes < 0 || endMinutes >= minutesPerDay ||
        startMinutes == endMinutes) {
        throw std::invalid_argument("work hours");
    }
    return endMinutes > startMinutes
        ? endMinutes - startMinutes
        : minutesPerDay - startMinutes + endMinutes;
}

inline int EndMinutesFromDuration(int startMinutes, int durationMinutes) {
    constexpr int minutesPerDay = 24 * 60;
    if (startMinutes < 0 || startMinutes >= minutesPerDay) {
        throw std::invalid_argument("start time");
    }
    if (durationMinutes <= 0 || durationMinutes >= minutesPerDay) {
        throw std::invalid_argument("work duration");
    }
    return (startMinutes + durationMinutes) % minutesPerDay;
}

inline int DurationMinutesFromHours(double hours) {
    if (!std::isfinite(hours) || hours <= 0.0 || hours >= 24.0) {
        throw std::invalid_argument("work duration hours");
    }
    const int minutes = static_cast<int>(std::round(hours * 60.0));
    if (minutes <= 0 || minutes >= 24 * 60) {
        throw std::invalid_argument("work duration hours");
    }
    return minutes;
}

inline int DeductedLunchMinutes(int lunchDuration, bool deduct) {
    if (lunchDuration < 0 || lunchDuration >= 1440) {
        throw std::invalid_argument("lunch duration");
    }
    return deduct ? lunchDuration : 0;
}

inline int EffectiveWorkMinutes(int start, int end, int lunchDuration, bool deduct) {
    const int duration = WorkDurationMinutes(start, end);
    const int effective = duration - DeductedLunchMinutes(lunchDuration, deduct);
    if (effective <= 0) throw std::invalid_argument("no working time");
    return effective;
}

inline int EndMinutesWithLunch(int start, int duration, int lunchDuration, bool deduct) {
    // Entered hours describe the full paid shift span. Lunch only changes the
    // displayed countdown; enabling deduction must not move the scheduled end.
    const int end = EndMinutesFromDuration(start, duration);
    (void)EffectiveWorkMinutes(start, end, lunchDuration, deduct);
    return end;
}

inline WorkdaySnapshot CalculateWorkday(
    int nowSeconds,
    int startMinutes,
    int endMinutes,
    double dailySalary,
    int lunchDuration = 0, bool deductLunch = false) {
    constexpr int secondsPerDay = 24 * 60 * 60;

    if (nowSeconds < 0 || nowSeconds >= secondsPerDay) {
        throw std::out_of_range("nowSeconds");
    }
    if (startMinutes < 0 || startMinutes >= 24 * 60 ||
        endMinutes < 0 || endMinutes >= 24 * 60 ||
        startMinutes == endMinutes) {
        throw std::invalid_argument("work hours");
    }
    if (dailySalary < 0) {
        throw std::out_of_range("dailySalary");
    }

    const int startSeconds = startMinutes * 60;
    const int endSeconds = endMinutes * 60;
    const bool spansMidnight = endSeconds < startSeconds;
    const int duration = spansMidnight
        ? secondsPerDay - startSeconds + endSeconds
        : endSeconds - startSeconds;

    WorkdayPhase phase = WorkdayPhase::AfterWork;
    int elapsed = duration;
    int remaining = 0;

    if (!spansMidnight) {
        if (nowSeconds < startSeconds) {
            phase = WorkdayPhase::BeforeWork;
            elapsed = 0;
            remaining = startSeconds - nowSeconds;
        } else if (nowSeconds < endSeconds) {
            phase = WorkdayPhase::Working;
            elapsed = nowSeconds - startSeconds;
            remaining = endSeconds - nowSeconds;
        }
    } else if (nowSeconds >= startSeconds || nowSeconds < endSeconds) {
        phase = WorkdayPhase::Working;
        elapsed = nowSeconds >= startSeconds
            ? nowSeconds - startSeconds
            : secondsPerDay - startSeconds + nowSeconds;
        remaining = duration - elapsed;
    }

    // Preserve the existing lunch-duration validation; all scheduled time is paid.
    (void)EffectiveWorkMinutes(startMinutes, endMinutes, lunchDuration, deductLunch);
    const double progress = std::clamp(
        static_cast<double>(elapsed) / static_cast<double>(duration), 0.0, 1.0);
    const double earned = dailySalary * progress;
    // No lunch interval is tracked: subtract the full lunch throughout Working,
    // including before/after noon and near shift end, clamping at zero. A zero
    // display never ends the shift early or changes earnings/reminder timing.
    // BeforeWork still counts down to clock-in without any lunch deduction.
    const int countdown = phase == WorkdayPhase::Working
        ? std::max(0, remaining - DeductedLunchMinutes(lunchDuration, deductLunch) * 60)
        : remaining;
    return {phase, remaining, earned, progress, countdown};
}

}  // namespace offwork
