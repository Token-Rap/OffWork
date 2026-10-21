#pragma once

#include <string>
#include <string_view>

namespace offwork {

// Apply to every formatted money amount outside the edit form, including any
// future tray/notification amounts. Keep labels outside the formatted amount.
inline std::wstring MoneyDisplayText(std::wstring_view formattedAmount, bool hideEarned) {
    return hideEarned ? L"****" : std::wstring(formattedAmount);
}

}  // namespace offwork
