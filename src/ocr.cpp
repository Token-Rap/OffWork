#include "ocr.h"
#include "ocr_time.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace offwork {
OcrResult RecognizeScreenshot(const std::wstring& path) {
    OcrResult result;
    try {
        auto extension = std::filesystem::path(path).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (extension != L".png" && extension != L".jpg" &&
            extension != L".jpeg" && extension != L".bmp") {
            result.error = L"请拖入 PNG、JPG 或 BMP 格式的打卡截图。";
            return result;
        }
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct ApartmentScope { ~ApartmentScope() { winrt::uninit_apartment(); } } apartment;
        using namespace winrt::Windows;
        auto engine = Media::Ocr::OcrEngine::TryCreateFromLanguage(Globalization::Language(L"zh-Hans"));
        if (!engine) engine = Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine) {
            result.error = L"Windows 未安装可用的文字识别语言，请在系统语言设置中安装简体中文 OCR。";
            return result;
        }
        const auto file = Storage::StorageFile::GetFileFromPathAsync(
            std::filesystem::absolute(path).wstring()).get();
        const auto stream = file.OpenAsync(Storage::FileAccessMode::Read).get();
        const auto decoder = Graphics::Imaging::BitmapDecoder::CreateAsync(stream).get();
        const auto limit = Media::Ocr::OcrEngine::MaxImageDimension();
        if (decoder.PixelWidth() > limit || decoder.PixelHeight() > limit) {
            result.error = L"截图尺寸过大，请裁剪打卡结果后重试。";
            return result;
        }
        const auto bitmap = decoder.GetSoftwareBitmapAsync(
            Graphics::Imaging::BitmapPixelFormat::Bgra8,
            Graphics::Imaging::BitmapAlphaMode::Ignore).get();
        const auto recognized = engine.RecognizeAsync(bitmap).get();
        // Keep OCR lines: Result.Text flattens rows and loses schedule context.
        for (const auto& line : recognized.Lines()) {
            if (!result.rawText.empty()) result.rawText += L'\n';
            result.rawText += line.Text();
        }
        result.minutes = ParseClockInTime(result.rawText);
        if (!result.minutes) result.error = L"未找到有效的上班打卡时间，请拖入包含打卡成功时间的清晰截图。";
    } catch (const winrt::hresult_error&) {
        result.error = L"无法识别截图，请确认文件可读取且图片完整，并在 Windows 语言设置中安装简体中文 OCR 后重试。";
    } catch (const std::exception&) {
        result.error = L"截图读取失败，请检查文件后重试。";
    }
    return result;
}
}  // namespace offwork
