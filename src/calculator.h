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
    bool onLunch = false;
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

// Offsets are relative to clock-in so overnight shifts/lunches work as well.
// A pair of -1 values preserves legacy settings with no lunch.
inline int LunchDurationMinutes(int start, int end, int lunchStart, int lunchEnd) {
    const int duration = WorkDurationMinutes(start, end);
    if (lunchStart == -1 && lunchEnd == -1) return 0;
    const int lunch = WorkDurationMinutes(lunchStart, lunchEnd);
    const int offset = (lunchStart - start + 1440) % 1440;
    if (offset + lunch > duration || lunch >= duration)
        throw std::invalid_argument("lunch outside shift");
    return lunch;
}

inline int EffectiveWorkMinutes(int start, int end, int lunchStart, int lunchEnd) {
    return WorkDurationMinutes(start, end) - LunchDurationMinutes(start, end, lunchStart, lunchEnd);
}

inline WorkdaySnapshot CalculateWorkday(
    int nowSeconds,
    int startMinutes,
    int endMinutes,
    double dailySalary,
    int lunchStart = -1, int lunchEnd = -1) {
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

    const int lunchDuration = LunchDurationMinutes(startMinutes, endMinutes, lunchStart, lunchEnd) * 60;
    const int lunchOffset = ((lunchStart - startMinutes + 1440) % 1440) * 60;
    const bool onLunch = phase == WorkdayPhase::Working && lunchDuration > 0 &&
        elapsed >= lunchOffset && elapsed < lunchOffset + lunchDuration;
    const int lunchElapsed = lunchDuration == 0 ? 0 :
        std::clamp(elapsed - lunchOffset, 0, lunchDuration);
    const double progress = std::clamp(
        static_cast<double>(elapsed - lunchElapsed) /
            static_cast<double>(duration - lunchDuration), 0.0, 1.0);
    const double earned = dailySalary * progress;
    return {phase, remaining, earned, progress, onLunch};
}

}  // namespace offwork
