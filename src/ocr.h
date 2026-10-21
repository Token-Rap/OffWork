#pragma once
#include <optional>
#include <string>

namespace offwork {
struct OcrResult {
    std::wstring rawText;
    std::optional<int> minutes;
    std::wstring error;
};
// Blocking operation: call on a worker thread. Owns its WinRT apartment.
OcrResult RecognizeScreenshot(const std::wstring& path);
}  // namespace offwork
