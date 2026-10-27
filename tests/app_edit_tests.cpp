#include "app_edit.h"
#include "app_view.h"
#include "calculator.h"
#include "check.h"
#include <filesystem>
#include <cstdint>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {

void SettingsApplyOnFocusLoss() {
    using namespace offwork;
    g.settings = Settings{};
    g.settings.hideEarned = true;
    g.expanded = true;
    PopulateFields();
    SaveSettings(g.settingsPath, g.settings);
    const auto edit = [](int index, const wchar_t* text) {
        SetFocusedField(index);
        g.fields[index] = MakeField(text);
        SetFocusedField(-1);
    };
    edit(0, L"08:30");
    CHECK(g.settings.startMinutes == 510);
    CHECK(LoadSettings(g.settingsPath).startMinutes == 510);
    CHECK(BuildModel().settings.startMinutes == 510);
    edit(1, L"17:30");
    CHECK(g.settings.endMinutes == 1050);
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1050);
    edit(2, L"1234.56");
    CHECK(g.settings.dailySalary == 1234.56);
    CHECK(LoadSettings(g.settingsPath).dailySalary == 1234.56);
    edit(2, L"1234.567");
    CHECK(g.settings.dailySalary == 1234.57);
    CHECK(BuildModel().settings.dailySalary == 1234.57);
    CHECK(LoadSettings(g.settingsPath).dailySalary == 1234.57);
    edit(3, L"11:30");
    edit(4, L"13:00");
    CHECK(g.settings.lunchStartMinutes == 690 && g.settings.lunchEndMinutes == 780);
    CHECK(LoadSettings(g.settingsPath).lunchStartMinutes == 690);
    CHECK(BuildModel().settings.lunchEndMinutes == 780);
    SetEndInputMode(EndInputMode::WorkDuration);
    edit(1, L"8.5");
    CHECK(g.settings.endMinutes == 1020);
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1020);
    CHECK(g.settings.hideEarned && LoadSettings(g.settingsPath).hideEarned);
    // Closing the panel also applies the final edit.
    SetFocusedField(4);
    g.fields[4] = MakeField(L"13:30");
    SetExpanded(false);
    CHECK(g.settings.lunchEndMinutes == 810);
    CHECK(LoadSettings(g.settingsPath).lunchEndMinutes == 810);
}

void InvalidEditsRevertWithoutChangingSavedSettings() {
    using namespace offwork;
    g.settings = Settings{};
    g.expanded = true;
    PopulateFields();
    SaveSettings(g.settingsPath, g.settings);
    const wchar_t* valid[] = {L"09:00", L"18:00", L"500", L"12:00", L"13:30"};
    const auto reject = [&](int index, const wchar_t* text) {
        SetFocusedField(index);
        g.fields[index] = MakeField(text);
        SetFocusedField(-1);
        CHECK(g.fields[index].text == valid[index]);
        const auto saved = LoadSettings(g.settingsPath);
        for (const auto& settings : {g.settings, saved, BuildModel().settings}) {
            CHECK(settings.startMinutes == 540 && settings.endMinutes == 1080);
            CHECK(settings.dailySalary == 500);
            CHECK(settings.lunchStartMinutes == 720 && settings.lunchEndMinutes == 810);
        }
    };
    for (const auto text : {L"", L"25:00", L"18:00", L"09:00x", L"999999999999999:00"}) reject(0, text);
    for (const auto text : {L"", L"09:00", L"09:30", L"23:60", L"10:00x"}) reject(1, text);
    for (const auto text : {L"", L"abc", L"nan", L"inf", L"1e9999", L"-1", L"1000001", L"500x"}) reject(2, text);
    for (const auto text : {L"-1", L"60.5", L"24:00", L"08:59", L"13:30", L"14:00", L"1:00", L"12:0", L"12:00x"}) reject(3, text);
    for (const auto text : {L"12:00", L"11:00", L"18:01", L"24:00", L"13:60", L"13:3"}) reject(4, text);
    SetEndInputMode(EndInputMode::WorkDuration);
    for (const auto text : {L"", L"0", L"24", L"0.001", L"nan", L"1e9999", L"1", L"8x"}) {
        SetFocusedField(1);
        g.fields[1] = MakeField(text);
        SetFocusedField(-1);
        CHECK(g.fields[1].text == L"9");
        CHECK(g.settings.endMinutes == 1080 && LoadSettings(g.settingsPath).endMinutes == 1080);
    }
    g.expanded = false;
}

void OvernightLunchCanBeEnteredAsAPair() {
    using namespace offwork;
    for (const int first : {3, 4}) {
        g.settings = Settings{};
        g.settings.startMinutes = 1320;
        g.settings.endMinutes = 360;
        g.settings.lunchStartMinutes = g.settings.lunchEndMinutes = -1;
        g.focusedField = -1;
        PopulateFields();
        SaveSettings(g.settingsPath, g.settings);
        SetFocusedField(first);
        g.fields[first] = MakeField(first == 3 ? L"23:30" : L"00:30");
        ApplyLiveLunchChange();
        CHECK(g.settings.lunchStartMinutes == -1);
        SetFocusedField(first == 3 ? 4 : 3);
        CHECK(g.fields[first].text == (first == 3 ? L"23:30" : L"00:30"));
        g.fields[first == 3 ? 4 : 3] = MakeField(first == 3 ? L"00:30" : L"23:30");
        ApplyLiveLunchChange();
        CHECK(g.settings.lunchStartMinutes == 1410 && g.settings.lunchEndMinutes == 30);
        SetFocusedField(-1);
        CHECK(LoadSettings(g.settingsPath).lunchEndMinutes == 30);
        CHECK(g.settings.startMinutes == 1320 && g.settings.endMinutes == 360);
        // Leaving an incomplete pair restores both saved endpoints.
        SetFocusedField(3);
        g.fields[3] = MakeField(L"23:");
        SetFocusedField(4);
        SetFocusedField(2); // The unchanged end must still revert the start draft.
        CHECK(g.fields[3].text == L"23:30" && g.fields[4].text == L"00:30");
        SetFocusedField(3);
        g.fields[3] = MakeField(L"23:");
        SetFocusedField(4);
        g.fields[4] = MakeField(L"08:00");
        SetFocusedField(2);
        CHECK(g.fields[3].text == L"23:30" && g.fields[4].text == L"00:30");
        CHECK(LoadSettings(g.settingsPath).lunchStartMinutes == 1410);
        SetFocusedField(-1);
    }
}

bool SamePixels(HDC first, HDC second, const RECT& rect) {
    GdiFlush();
    DIBSECTION a = {}, b = {};
    CHECK(GetObjectW(GetCurrentObject(first, OBJ_BITMAP), sizeof(a), &a));
    CHECK(GetObjectW(GetCurrentObject(second, OBJ_BITMAP), sizeof(b), &b));
    const auto* left = static_cast<const std::uint32_t*>(a.dsBm.bmBits);
    const auto* right = static_cast<const std::uint32_t*>(b.dsBm.bmBits);
    for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left; x < rect.right; ++x) {
            const auto index = static_cast<std::size_t>(y) * a.dsBm.bmWidth + x;
            if (((left[index] ^ right[index]) & 0x00ffffff) != 0) return false;
        }
    }
    return true;
}

void LunchFreezesCountdownAndEarningsOnScreen() {
    using namespace offwork;
    for (const int scale : {75, 100, 150}) {
        for (const UINT dpi : {96u, 144u, 192u}) {
            RenderModel model;
            model.settings.dailySalary = 750;
            model.settings.uiScalePercent = scale;
            model.dpi = dpi;
            auto fonts = CreateFonts(scale, dpi);
            model.fonts = &fonts;
            const SIZE size = WindowSizeFor(scale, dpi, false);
            LayeredSurface first, second;
            CHECK(first.EnsureSize(size.cx, size.cy) && second.EnsureSize(size.cx, size.cy));
            model.now.wHour = 12;
            RenderFrame(first.dc(), model);
            model.now.wHour = 13;
            model.now.wSecond = 59;
            RenderFrame(second.dc(), model);
            for (const auto rect : {kCountdownText, kEarnedAmount, kProgressText, kProgressTrackRect})
                CHECK(SamePixels(first.dc(), second.dc(), ScaleRect(rect, scale, dpi)));
            model.now.wMinute = 30;
            model.now.wSecond = 0;
            RenderFrame(second.dc(), model);
            CHECK(!SamePixels(first.dc(), second.dc(), ScaleRect(kCountdownText, scale, dpi)));
            CHECK(SamePixels(first.dc(), second.dc(), ScaleRect(kEarnedAmount, scale, dpi)));
            model.now.wMinute = 31;
            RenderFrame(second.dc(), model);
            CHECK(!SamePixels(first.dc(), second.dc(), ScaleRect(kEarnedAmount, scale, dpi)));
            DestroyFonts(fonts);
        }
    }
}

void AllMoneyOutsideEditorIsMasked() {
    using namespace offwork;
    for (const bool expanded : {false, true}) {
        for (const int scale : {75, 100, 150}) {
            for (const UINT dpi : {96u, 144u, 192u}) {
                RenderModel model;
                model.expanded = expanded;
                model.settings.uiScalePercent = scale;
                model.dpi = dpi;
                model.now.wHour = 12;
                model.salaryField = MakeField(L"500.00");
                auto fonts = CreateFonts(scale, dpi);
                model.fonts = &fonts;
                const SIZE size = WindowSizeFor(scale, dpi, expanded);
                LayeredSurface first, second;
                CHECK(first.EnsureSize(size.cx, size.cy));
                CHECK(second.EnsureSize(size.cx, size.cy));
                const RECT earned = ScaleRect(kEarnedAmount, scale, dpi);
                const RECT salary = ScaleRect(kSalaryText, scale, dpi);
                const RECT editor = ScaleRect(kSalaryBox, scale, dpi);
                for (const bool hide : {true, false}) {
                    model.settings.hideEarned = hide;
                    model.settings.dailySalary = 500.0;
                    RenderFrame(first.dc(), model);
                    model.settings.dailySalary = 987654.32;
                    RenderFrame(second.dc(), model);
                    CHECK(SamePixels(first.dc(), second.dc(), earned) == hide);
                    CHECK(SamePixels(first.dc(), second.dc(), salary) == hide);
                    if (expanded) {
                        CHECK(SamePixels(first.dc(), second.dc(), editor));
                        model.salaryField = MakeField(L"987654.32");
                        RenderFrame(second.dc(), model);
                        CHECK(!SamePixels(first.dc(), second.dc(), editor));
                        model.salaryField = MakeField(L"500.00");
                    }
                }
                DestroyFonts(fonts);
            }
        }
    }
}

void SmallControlsAndMaskRenderCorrectly() {
    using namespace offwork;
    for (const bool expanded : {false, true}) {
        for (const int scale : {75, 100, 150}) {
            for (const UINT dpi : {96u, 144u, 192u}) {
                RenderModel m;
                m.expanded = expanded;
                m.settings.uiScalePercent = scale;
                m.dpi = dpi;
                const auto center = [&](const LogicalRect& r) -> POINT {
                    return {ScaleValue((r.left + r.right) / 2, scale, dpi),
                            ScaleValue((r.top + r.bottom) / 2, scale, dpi)};
                };
                CHECK(HitTest(center(kEyeButton), m) == HotElement::HideEarned);
                if (expanded) {
                    // The former Save button area is no longer interactive.
                    CHECK(HitTest(POINT{ScaleValue(180, scale, dpi), ScaleValue(613, scale, dpi)}, m)
                          == HotElement::None);
                    CHECK(HitTest(center(kLunchStartBox), m) == HotElement::LunchStartField);
                    CHECK(FieldIndexOf(HotElement::LunchStartField) == 3);
                    CHECK(HitTest(center(kLunchEndBox), m) == HotElement::LunchEndField);
                    CHECK(FieldIndexOf(HotElement::LunchEndField) == 4);
                    CHECK(HitTest(POINT{ScaleValue(300, scale, dpi), ScaleValue(503, scale, dpi)}, m)
                          == HotElement::None); // Removed lunch checkbox.
                    CHECK(HitTest(POINT{ScaleValue(100, scale, dpi), ScaleValue(470, scale, dpi)}, m)
                          == HotElement::LunchStartField);
                }
                CHECK(HitTest(center(kUpdateButton), m) == HotElement::None);
                m.updateAvailable = true;
                CHECK(HitTest(center(kUpdateButton), m) == HotElement::Update);
                m.updateDownloading = true;
                CHECK(HitTest(center(kUpdateButton), m) == HotElement::None);
                m.screenshotDragActive = true;
                CHECK(HitTest(center(kEyeButton), m) == HotElement::None);
                m.screenshotDragActive = false;
                auto fonts = CreateFonts(scale, dpi);
                m.fonts = &fonts;
                const auto size = WindowSizeFor(scale, dpi, expanded);
                LayeredSurface a, b;
                CHECK(a.EnsureSize(size.cx, size.cy) && b.EnsureSize(size.cx, size.cy));
                m.settings.hideEarned = true;
                m.now.wHour = 10;
                RenderFrame(a.dc(), m);
                if (expanded) {
                    CHECK(GetPixel(a.dc(), ScaleValue(180, scale, dpi),
                                   ScaleValue(613, scale, dpi)) == kPanelBackground);
                }
                // Download activity has a ring on the indicator, not just a
                // dimmed arrow. This point is on the ring above the arrow.
                CHECK(GetPixel(a.dc(), ScaleValue(253, scale, dpi),
                               ScaleValue(16, scale, dpi)) != kTitleBackground);
                const auto checkRingChange = [&](int from, int to, unsigned phase) {
                    m.updatePercent = from;
                    m.updateProgressPhase = 0;
                    RenderFrame(a.dc(), m);
                    m.updatePercent = to;
                    m.updateProgressPhase = phase;
                    RenderFrame(b.dc(), m);
                    GdiFlush();
                    DIBSECTION first = {}, second = {};
                    CHECK(GetObjectW(GetCurrentObject(a.dc(), OBJ_BITMAP), sizeof(first), &first));
                    CHECK(GetObjectW(GetCurrentObject(b.dc(), OBJ_BITMAP), sizeof(second), &second));
                    const auto* left = static_cast<const std::uint32_t*>(first.dsBm.bmBits);
                    const auto* right = static_cast<const std::uint32_t*>(second.dsBm.bmBits);
                    const RECT indicator = ScaleRect(kUpdateButton, scale, dpi);
                    bool changed = false;
                    for (int y = 0; y < size.cy; ++y) {
                        for (int x = 0; x < size.cx; ++x) {
                            const auto index = static_cast<std::size_t>(y) * size.cx + x;
                            if (((left[index] ^ right[index]) & 0x00ffffff) == 0) continue;
                            changed = true;
                            CHECK(PtInRect(&indicator, POINT{x, y}));
                        }
                    }
                    CHECK(changed);
                };
                checkRingChange(0, 75, 0);
                checkRingChange(75, 100, 0);
                checkRingChange(-1, -1, 6);
                RenderFrame(a.dc(), m);
                m.now.wHour = 16;
                RenderFrame(b.dc(), m);
                const RECT amount = ScaleRect(kEarnedAmount, scale, dpi);
                for (int y = amount.top; y < amount.bottom; ++y) {
                    for (int x = amount.left; x < amount.right; ++x) {
                        CHECK(GetPixel(a.dc(), x, y) == GetPixel(b.dc(), x, y));
                    }
                }
                m.settings.hideEarned = false;
                RenderFrame(b.dc(), m);
                bool different = false;
                for (int y = amount.top; y < amount.bottom; ++y) {
                    for (int x = amount.left; x < amount.right; ++x) {
                        different |= GetPixel(a.dc(), x, y) != GetPixel(b.dc(), x, y);
                    }
                }
                CHECK(different);
                DestroyFonts(fonts);
            }
        }
    }
}

void DropTargetRendersOnTheLayeredSurface() {
    using namespace offwork;
    for (const bool expanded : {false, true}) {
        for (const int scale : {75, 100, 150}) {
            for (const UINT dpi : {96u, 144u, 192u}) {
                RenderModel model;
                model.settings.uiScalePercent = scale;
                model.expanded = expanded;
                model.dpi = dpi;
                model.screenshotDragActive = true;
                const SIZE size = WindowSizeFor(scale, dpi, expanded);
                LayeredSurface surface;
                CHECK(surface.EnsureSize(size.cx, size.cy));
                RenderFrame(surface.dc(), model); // Target needs no text/fonts.
                const auto s = [&](int value) { return ScaleValue(value, scale, dpi); };
                CHECK(GetPixel(surface.dc(), s(30), s(30)) == kDropBackground);
                bool dash = false;
                bool gap = false;
                for (int x = s(40); x < size.cx - s(40); ++x) {
                    const auto pixel = GetPixel(surface.dc(), x, s(14));
                    dash |= pixel == kAccent;
                    gap |= pixel == kDropBackground;
                }
                CHECK(dash && gap);
                CHECK(GetPixel(surface.dc(), s(156), size.cy / 2) == kAccent);
                surface.ApplyRoundedAlpha(s(kCornerRadius));
                GdiFlush();
                DIBSECTION dib = {};
                CHECK(GetObjectW(GetCurrentObject(surface.dc(), OBJ_BITMAP), sizeof(dib), &dib) != 0);
                const auto* bytes = static_cast<const std::uint8_t*>(dib.dsBm.bmBits);
                CHECK(bytes != nullptr && bytes[3] == 0); // Rounded transparent corner.
                const auto center = (static_cast<std::size_t>(size.cy / 2) * size.cx + size.cx / 2) * 4;
                CHECK(bytes[center + 3] == 255);

                model.screenshotDragActive = false;
                auto fonts = CreateFonts(scale, dpi);
                model.fonts = &fonts;
                RenderFrame(surface.dc(), model);
                // The old always-visible hint area is now plain background.
                CHECK(GetPixel(surface.dc(), s(180), s(252)) == kBackground);
                DestroyFonts(fonts);
            }
        }
    }
}

void CompleteOcr(offwork::OcrResult result) {
    std::promise<offwork::OcrResult> promise;
    offwork::g.ocrTask = promise.get_future();
    promise.set_value(std::move(result));
    offwork::PollScreenshotOcr();
    CHECK(!offwork::g.ocrTask.valid());
}

void OcrSilentlyUpdatesAndSavesTime() {
    using namespace offwork;
    const auto path = std::filesystem::temp_directory_path() /
        (L"offwork-ocr-edit-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    g.settingsPath = path.wstring();
    g.settings = Settings{};
    PopulateFields();
    CompleteOcr({L"上班打卡 08:55", 535, {}});
    CHECK(g.settings.startMinutes == 535 && g.settings.endMinutes == 1080);
    CHECK(g.fields[0].text == L"08:55");
    CHECK(LoadSettings(g.settingsPath).startMinutes == 535);
    CHECK(g.footerText == kDefaultFooter && !g.footerIsError);

    g.settings = Settings{};
    g.settings.endInputMode = EndInputMode::WorkDuration;
    PopulateFields();
    CompleteOcr({{}, 535, {}});
    CHECK(g.settings.startMinutes == 535 && g.settings.endMinutes == 1075);
    CHECK(g.fields[1].text == L"9");
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1075);

    g.footerText = L"unchanged";
    CompleteOcr({{}, {}, L"unrecognized screenshot"});
    CHECK(g.settings.startMinutes == 535);
    CHECK(g.footerText == L"unchanged" && !g.footerIsError);
    CHECK(LoadSettings(g.settingsPath).startMinutes == 535);

    g.settings.endInputMode = EndInputMode::EndTime;
    CompleteOcr({{}, g.settings.endMinutes, {}}); // Conflicting schedule.
    CHECK(g.settings.startMinutes == 535 && g.footerText == L"unchanged");
    std::promise<OcrResult> failed;
    g.ocrTask = failed.get_future();
    failed.set_exception(std::make_exception_ptr(std::runtime_error("OCR worker")));
    PollScreenshotOcr();
    CHECK(!g.ocrTask.valid() && g.settings.startMinutes == 535);
    CHECK(g.footerText == L"unchanged");
    std::filesystem::remove(path);
}

void EndTimeEditsApplyBeforeOcrRepopulatesFields() {
    using namespace offwork;
    g.settings = Settings{};
    g.settings.endMinutes = 1130; // Evidence: 18:50 in fixed-end mode.
    g.expanded = true;
    g.focusedField = -1;
    PopulateFields();
    SaveSettings(g.settingsPath, g.settings);
    SetFocusedField(1);
    g.fields[1] = InsertText(SelectAll(g.fields[1]), L"19:10", FilterFor(1), MaxLengthFor(1));
    CHECK(HandleFieldKey(VK_END)); // Moving the caret must not require blur to save.
    CHECK(g.focusedField == 1 && g.fields[1].caret == 5);
    CHECK(g.settings.endMinutes == 1150);
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1150);
    CHECK(BuildModel().settings.endMinutes == 1150);

    CompleteOcr({L"上班打卡 08:52", 532, {}});
    CHECK(g.settings.startMinutes == 532 && g.settings.endMinutes == 1150);
    CHECK(g.fields[0].text == L"08:52" && BuildModel().endField.text == L"19:10");
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1150);
    const auto snapshot = CalculateWorkday(13 * 3600 + 27 * 60 + 53,
        g.settings.startMinutes, g.settings.endMinutes, 0,
        g.settings.lunchStartMinutes, g.settings.lunchEndMinutes);
    CHECK(snapshot.remainingSeconds == 20527); // 05:42:07, using the edited end.

    SetFocusedField(1);
    g.fields[1] = MakeField(L"18:5");
    ApplyLiveEndChange();
    CHECK(g.settings.endMinutes == 1150);
    g.fields[1] = InsertText(g.fields[1], L"0", FilterFor(1), MaxLengthFor(1));
    ApplyLiveEndChange(); // Same live-save path used by WM_CHAR.
    CHECK(g.settings.endMinutes == 1130 && LoadSettings(g.settingsPath).endMinutes == 1130);
    CHECK(g.focusedField == 1 && g.fields[1].caret == 5);
    CHECK(HandleFieldKey(VK_BACK)); // Partial 18:5 must not change the saved end.
    CHECK(g.fields[1].text == L"18:5" && g.settings.endMinutes == 1130);
    for (const auto text : {L"08:52", L"10:00", L"25:00"}) { // Equal start, excludes lunch, invalid clock.
        g.fields[1] = MakeField(text);
        CHECK(HandleFieldKey(VK_END));
        CHECK(g.settings.endMinutes == 1130 && LoadSettings(g.settingsPath).endMinutes == 1130);
    }
    SetFocusedField(-1);
    CHECK(g.fields[1].text == L"18:50");

    SetEndInputMode(EndInputMode::WorkDuration);
    SetFocusedField(1);
    g.fields[1] = MakeField(L"8.5");
    CHECK(HandleFieldKey(VK_END));
    CHECK(g.settings.endMinutes == 1042 && LoadSettings(g.settingsPath).endMinutes == 1042);
    CHECK(g.fields[1].text == L"8.5" && g.fields[1].caret == 3);
    CompleteOcr({{}, 535, {}});
    CHECK(g.settings.endMinutes == 1045 && g.fields[1].text == L"8.5");
    CHECK(BuildModel().settings.endMinutes == 1045);
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1045);

    SetFocusedField(1);
    CHECK(HandleFieldKey(VK_BACK)); // 8. is incomplete; preserve 8.5 hours.
    CHECK(g.fields[1].text == L"8." && g.settings.endMinutes == 1045);
    CHECK(HandleFieldKey(VK_BACK)); // 8 is complete and valid.
    CHECK(g.settings.endMinutes == 1015 && LoadSettings(g.settingsPath).endMinutes == 1015);
    SetFocusedField(-1);

    g.settings.startMinutes = 1320; // Overnight, no lunch.
    g.settings.endMinutes = 360;
    g.settings.lunchStartMinutes = g.settings.lunchEndMinutes = -1;
    PopulateFields();
    SetFocusedField(1);
    g.fields[1] = MakeField(L"9");
    ApplyLiveEndChange();
    CHECK(g.settings.endMinutes == 420);
    CompleteOcr({{}, 1380, {}});
    CHECK(g.settings.startMinutes == 1380 && g.settings.endMinutes == 480);
    CHECK(g.fields[1].text == L"9" && LoadSettings(g.settingsPath).endMinutes == 480);

    g.settings.startMinutes = 1320;
    g.settings.endMinutes = 360;
    g.settings.endInputMode = EndInputMode::EndTime;
    PopulateFields();
    SetFocusedField(1);
    g.fields[1] = MakeField(L"7:30"); // Existing time parser accepts a single hour digit.
    CHECK(HandleFieldKey(VK_END));
    CHECK(g.settings.endMinutes == 450 && LoadSettings(g.settingsPath).endMinutes == 450);
    CompleteOcr({{}, 1380, {}});
    CHECK(g.settings.endMinutes == 450 && g.fields[1].text == L"07:30");
}

}  // namespace

int main(int argc, char** argv) {
    using namespace offwork;
    const auto settingsPath = std::filesystem::temp_directory_path() /
        (L"offwork-live-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    g.settingsPath = settingsPath.wstring();
    if (argc == 2 && std::string(argv[1]) == "--ocr-endtime") {
        EndTimeEditsApplyBeforeOcrRepopulatesFields();
        OcrSilentlyUpdatesAndSavesTime();
        std::filesystem::remove(settingsPath);
        std::cout << "All OCR/end-time editor tests passed.\n";
        return 0;
    }
    EndTimeEditsApplyBeforeOcrRepopulatesFields();
    SettingsApplyOnFocusLoss();
    InvalidEditsRevertWithoutChangingSavedSettings();
    OvernightLunchCanBeEnteredAsAPair();
    g.settings = Settings{};
    PopulateFields();
    CHECK(g.fields[3].text == L"12:00" && g.fields[4].text == L"13:30");
    CHECK(IsCharAllowed(L':', FilterFor(3)) && IsCharAllowed(L':', FilterFor(4)));
    CHECK(!IsCharAllowed(L'.', FilterFor(3)));
    CHECK(MaxLengthFor(3) == 5 && MaxLengthFor(4) == 5);
    Settings parsed;
    CHECK(ReadFieldSettings(parsed));
    SetEndInputMode(EndInputMode::WorkDuration);
    CHECK(g.fields[1].text == L"9");
    g.fields[1] = MakeField(L"8.5");
    CHECK(ReadFieldSettings(parsed) && parsed.endMinutes == 1050);
    SetEndInputMode(EndInputMode::EndTime);
    CHECK(g.fields[1].text == L"17:30");
    // Valid complete lunch edits persist immediately, keeping focus/caret.
    g.focusedField = 3;
    g.fields[3] = MakeField(L"11:45");
    ApplyLiveLunchChange();
    CHECK(g.focusedField == 3 && g.fields[3].caret == 5);
    CHECK(g.settings.lunchStartMinutes == 705);
    CHECK(LoadSettings(g.settingsPath).lunchStartMinutes == 705);
    g.fields[4] = MakeField(L"13:0");
    ApplyLiveLunchChange();
    CHECK(g.settings.lunchEndMinutes == 810); // Partial edit never persists.
    g.fields[4] = MakeField(L"13:00");
    ApplyLiveLunchChange();
    CHECK(g.settings.lunchEndMinutes == 780 && g.settings.endMinutes == 1050);
    CHECK(LoadSettings(g.settingsPath).lunchEndMinutes == 780);
    // An invalid complete edit never changes persisted state.
    g.fields[4] = MakeField(L"11:30");
    ApplyLiveLunchChange();
    CHECK(g.settings.lunchEndMinutes == 780);
    CHECK(LoadSettings(g.settingsPath).lunchEndMinutes == 780);
    PopulateFields();
    g.expanded = true;
    g.focusedField = 4;
    FocusNextField(1);
    CHECK(g.focusedField == 0);
    FocusNextField(-1);
    CHECK(g.focusedField == 4);
    g.expanded = false;
    g.focusedField = -1;
    // Legacy zero durations remain editable: clear and then restore lunch.
    g.fields[3] = MakeField(L"");
    g.fields[4] = MakeField(L"");
    ApplyFieldChange(3);
    CHECK(g.settings.lunchStartMinutes == -1 && g.settings.lunchEndMinutes == -1);
    CHECK(g.fields[3].text.empty() && g.fields[4].text.empty());
    CHECK(LoadSettings(g.settingsPath).lunchStartMinutes == -1);
    g.fields[3] = MakeField(L"12:00");
    g.fields[4] = MakeField(L"13:00");
    ApplyFieldChange(4);
    CHECK(g.settings.lunchStartMinutes == 720 && g.settings.lunchEndMinutes == 780);
    CHECK(g.fields[3].text == L"12:00");
    OcrSilentlyUpdatesAndSavesTime();
    LunchFreezesCountdownAndEarningsOnScreen();
    DropTargetRendersOnTheLayeredSurface();
    AllMoneyOutsideEditorIsMasked();
    SmallControlsAndMaskRenderCorrectly();
    std::filesystem::remove(settingsPath);
    std::cout << "All lunch editor and silent OCR tests passed.\n";
}
