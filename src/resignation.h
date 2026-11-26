#pragma once

#include <chrono>
#include <optional>
#include <string_view>

namespace offwork {

// Strict local civil date, with no time zone or elapsed-hour arithmetic.
inline std::optional<std::chrono::sys_days> ParseResignationDate(std::wstring_view text) {
    if (text.size() != 10 || text[4] != L'-' || text[7] != L'-') return std::nullopt;
    int parts[3] = {};
    int part = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) { ++part; continue; }
        if (text[i] < L'0' || text[i] > L'9') return std::nullopt;
        parts[part] = parts[part] * 10 + text[i] - L'0';
    }
    if (parts[0] < 1) return std::nullopt;
    const std::chrono::year_month_day date{std::chrono::year{parts[0]},
        std::chrono::month{static_cast<unsigned>(parts[1])},
        std::chrono::day{static_cast<unsigned>(parts[2])}};
    if (!date.ok()) return std::nullopt;
    return std::chrono::sys_days{date};
}

// Calendar coverage: Monday-Friday only. The app has no holiday/make-up dataset.
// Count (today, target], return 0 on target itself, hide expired/invalid/disabled.
inline std::optional<int> RemainingResignationWorkdays(std::chrono::sys_days today,
    std::wstring_view targetText, bool shown) {
    if (!shown) return std::nullopt;
    const auto target = ParseResignationDate(targetText);
    if (!target || *target < today) return std::nullopt;
    const auto distance = (*target - today).count();
    int result = static_cast<int>(distance / 7) * 5;
    for (int offset = 1; offset <= distance % 7; ++offset) {
        const auto weekday = std::chrono::weekday{today + std::chrono::days{offset}};
        if (weekday != std::chrono::Saturday && weekday != std::chrono::Sunday) ++result;
    }
    return result;
}

}  // namespace offwork
