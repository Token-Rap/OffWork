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
    int remainingSeconds;
    double earned;
    double progress;
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
    (void)EndMinutesFromDuration(start, duration); // validate inputs
    // Clock-only schedules must remain shorter than a day, including lunch.
    return EndMinutesFromDuration(start, duration + DeductedLunchMinutes(lunchDuration, deduct));
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

    const int effectiveDuration = EffectiveWorkMinutes(
        startMinutes, endMinutes, lunchDuration, deductLunch) * 60;
    // Without a break interval there is no timed pause. Keep the real countdown
    // and credit effective work from the end backwards (see README).
    elapsed = phase == WorkdayPhase::BeforeWork ? 0 : effectiveDuration - remaining;
    const double progress = std::clamp(
        static_cast<double>(elapsed) / static_cast<double>(effectiveDuration), 0.0, 1.0);
    const double earned = dailySalary * progress;
    return {phase, remaining, earned, progress};
}

}  // namespace offwork
