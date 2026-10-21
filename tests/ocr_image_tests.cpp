#include "ocr.h"
#include "check.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Usage: OffWorkOcrImageTests <image>\n"; return 2; }
    const auto result = offwork::RecognizeScreenshot(std::filesystem::absolute(argv[1]).wstring());
    std::cout << "Raw OCR text:\n" << Utf8(result.rawText) << "\n";
    if (!result.error.empty()) std::cerr << Utf8(result.error) << "\n";
    if (result.minutes) std::cout << "Parsed time: " << (*result.minutes / 60 < 10 ? "0" : "") << *result.minutes / 60 << ':'
        << (*result.minutes % 60 < 10 ? "0" : "") << *result.minutes % 60
        << " (" << *result.minutes << " minutes)\n";
    CHECK(result.error.empty());
    CHECK(result.minutes == 535);
    CHECK(!offwork::RecognizeScreenshot(L"missing.png").error.empty());
    CHECK(!offwork::RecognizeScreenshot(L"unsupported.txt").error.empty());
    return 0;
}
