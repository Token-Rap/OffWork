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

inline bool HasLunch(int start, int end) {
    return start >= 0 && start < 1440 && end >= 0 && end < 1440 && start != end;
}

// Seconds of a daily repeating break intersecting [start, start + elapsed).
// Include the previous day's break for a shift beginning inside an overnight break.
inline int LunchOverlapSeconds(int start, int elapsed, int lunchStart, int lunchEnd) {
    if (!HasLunch(lunchStart, lunchEnd)) return 0;
    const int length = WorkDurationMinutes(lunchStart, lunchEnd) * 60;
    int overlap = 0;
    for (int day = -1; day <= 2; ++day) {
        const int begin = lunchStart * 60 + day * 86400;
        overlap += std::max(0, std::min(start + elapsed, begin + length) - std::max(start, begin));
    }
    return overlap;
}

inline int EffectiveWorkMinutes(int start, int end, int lunchStart, int lunchEnd, bool deduct) {
    const int duration = WorkDurationMinutes(start, end);
    const int effective = duration - (deduct
        ? LunchOverlapSeconds(start * 60, duration * 60, lunchStart, lunchEnd) / 60 : 0);
    if (effective <= 0) throw std::invalid_argument("no working time");
    return effective;
}

inline int EndMinutesWithLunch(int start, int duration, int lunchStart, int lunchEnd, bool deduct) {
    (void)EndMinutesFromDuration(start, duration); // validate inputs
    if (!deduct || !HasLunch(lunchStart, lunchEnd)) return EndMinutesFromDuration(start, duration);
    // Walk at minute precision, matching the persisted schedule. Reject days >= 24h
    // because a clock-only schedule cannot represent them unambiguously.
    int worked = 0;
    for (int elapsed = 1; elapsed < 1440; ++elapsed) {
        if (LunchOverlapSeconds(start * 60, elapsed * 60, lunchStart, lunchEnd) ==
            LunchOverlapSeconds(start * 60, (elapsed - 1) * 60, lunchStart, lunchEnd)) ++worked;
        if (worked == duration) return (start + elapsed) % 1440;
    }
    throw std::invalid_argument("shift must be shorter than 24h");
}

inline WorkdaySnapshot CalculateWorkday(
    int nowSeconds,
    int startMinutes,
    int endMinutes,
    double dailySalary,
    int lunchStart = -1, int lunchEnd = -1, bool deductLunch = false) {
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

    const int effectiveDuration = duration - (deductLunch
        ? LunchOverlapSeconds(startSeconds, duration, lunchStart, lunchEnd) : 0);
    if (effectiveDuration <= 0) throw std::invalid_argument("no working time");
    if (deductLunch) {
        elapsed -= LunchOverlapSeconds(startSeconds, elapsed, lunchStart, lunchEnd);
        if (phase == WorkdayPhase::Working) remaining = effectiveDuration - elapsed;
    }
    const double progress = std::clamp(
        static_cast<double>(elapsed) / static_cast<double>(effectiveDuration), 0.0, 1.0);
    const double earned = dailySalary * progress;
    return {phase, remaining, earned, progress};
}

}  // namespace offwork
