#pragma once

#include <string_view>

namespace offwork {

// Pure, locale-independent Windows filename check, shared by drag/drop and OCR.
inline bool IsSupportedImageFile(std::wstring_view path) {
    if (path.empty() || path.find(L'\0') != std::wstring_view::npos) return false;
    const auto slash = path.find_last_of(L"/\\");
    const auto name = path.substr(slash == std::wstring_view::npos ? 0 : slash + 1);
    const auto dot = name.find_last_of(L'.');
    if (dot == std::wstring_view::npos || dot == 0) return false;
    const auto extension = name.substr(dot);
    constexpr std::wstring_view extensions[] = {L".png", L".jpg", L".jpeg", L".bmp"};
    for (const auto expected : extensions) {
        if (extension.size() != expected.size()) continue;
        bool matches = true;
        for (std::size_t i = 0; i < extension.size(); ++i) {
            const auto c = extension[i];
            const auto lower = c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c;
            if (lower != expected[i]) { matches = false; break; }
        }
        if (matches) return true;
    }
    return false;
}

}  // namespace offwork
