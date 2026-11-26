#include "app_edit.h"
#include "app_view.h"
#include "calculator.h"
#include "check.h"
#include "resignation_calendar.h"
#include <commctrl.h>
#include <tuple>
#include <filesystem>
#include <cstdint>
#include <future>
#include <fstream>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>
#include "version.h"

namespace {

void NetDurationRecalculatesEnd() {
    using namespace offwork;
    g.settings = Settings{};
    g.settings.lunchEndMinutes = 780;
    g.focusedField = -1;
    PopulateFields();
    CHECK(g.settings.endMinutes == 1080); // Loading the editor never migrates end.
    CHECK(g.fields[1].text == L"8");
    g.fields[1] = MakeField(L"8.5");
    ApplyLiveEndChange();
    CHECK(g.settings.endMinutes == 1110);
    g.fields[0] = MakeField(L"08:55");
    ApplyFieldChange(0);
    CHECK(g.settings.endMinutes == 1105);
    g.fields[4] = MakeField(L"13:30");
    ApplyLiveLunchChange();
    CHECK(g.settings.endMinutes == 1135);
    CHECK(g.fields[1].text == L"8.5");
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1135);
    // Blur-only lunch edits also preserve net hours, including removing lunch.
    g.fields[4] = MakeField(L"13:00");
    ApplyFieldChange(4);
    CHECK(g.settings.endMinutes == 1105 && g.fields[1].text == L"8.5");
    g.fields[3] = MakeField(L"");
    g.fields[4] = MakeField(L"");
    ApplyFieldChange(3);
    CHECK(g.settings.endMinutes == 1045);
    g.fields[3] = MakeField(L"12:00");
    g.fields[4] = MakeField(L"13:00");
    ApplyFieldChange(4);
    CHECK(g.settings.endMinutes == 1105);
    // Reject a >=24-hour wall span without throwing or overwriting saved data.
    g.fields[1] = MakeField(L"23");
    ApplyLiveEndChange();
    CHECK(g.settings.endMinutes == 1105);
    ApplyFieldChange(1);
    CHECK(g.fields[1].text == L"8.5" && g.settings.endMinutes == 1105);
    g.fields[0] = MakeField(L"13:00"); // Lunch is now outside the shift.
    ApplyFieldChange(0);
    CHECK(g.settings.startMinutes == 535 && g.settings.endMinutes == 1105);
    SetEndInputMode(EndInputMode::EndTime);
    CHECK(g.fields[1].text == L"18:25");
    SetEndInputMode(EndInputMode::WorkDuration);
    CHECK(g.fields[1].text == L"8.5" && g.settings.endMinutes == 1105);
    // Formatting to two decimal hours still round-trips a single net minute.
    g.settings.lunchStartMinutes = g.settings.lunchEndMinutes = -1;
    g.settings.endMinutes = 536;
    PopulateFields();
    Settings draft;
    CHECK(g.fields[1].text == L"0.02" && ReadFieldSettings(draft));
    CHECK(draft.endMinutes == 536);
}

void SettingsApplyOnFocusLoss() {
    using namespace offwork;
    g.settings = Settings{};
    g.settings.endInputMode = EndInputMode::EndTime;
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
    CHECK(g.settings.endMinutes == 1110);
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1110);
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
    g.settings.endInputMode = EndInputMode::EndTime;
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
        CHECK(g.fields[1].text == L"7.5");
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
        g.settings.endInputMode = EndInputMode::EndTime;
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
                    // The gap above the footer is not interactive.
                    CHECK(HitTest(POINT{ScaleValue(180, scale, dpi), ScaleValue(730, scale, dpi)}, m)
                          == HotElement::None);
                    CHECK(HitTest(center(kLunchStartBox), m) == HotElement::LunchStartField);
                    CHECK(FieldIndexOf(HotElement::LunchStartField) == 3);
                    CHECK(HitTest(center(kLunchEndBox), m) == HotElement::LunchEndField);
                    CHECK(FieldIndexOf(HotElement::LunchEndField) == 4);
                    CHECK(HitTest(POINT{ScaleValue(300, scale, dpi), ScaleValue(503, scale, dpi)}, m)
                          == HotElement::DeductLunch); // Restored optional display toggle.
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
                                   ScaleValue(730, scale, dpi)) == kPanelBackground);
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

void OptionalLunchCountdownRendersOnlyBeforeLunchEnds() {
    using namespace offwork;
    for (const int scale : {75, 100, 150}) {
        for (const UINT dpi : {96u, 144u, 192u}) {
            RenderModel model;
            model.expanded = true;
            model.settings.hideEarned = true;
            model.salaryField = MakeField(L"****");
            model.settings.uiScalePercent = scale;
            model.dpi = dpi;
            auto fonts = CreateFonts(scale, dpi);
            model.fonts = &fonts;
            const auto size = WindowSizeFor(scale, dpi, true);
            LayeredSurface off, on;
            CHECK(off.EnsureSize(size.cx, size.cy) && on.EnsureSize(size.cx, size.cy));
            for (const int hour : {8, 11, 12, 13, 14, 18}) {
                model.now.wHour = static_cast<WORD>(hour);
                model.settings.deductLunch = false;
                RenderFrame(off.dc(), model);
                model.settings.deductLunch = true;
                RenderFrame(on.dc(), model);
                CHECK(SamePixels(off.dc(), on.dc(), ScaleRect(kCountdownText, scale, dpi)) == (hour != 11));
                for (const auto rect : {kEarnedAmount, kProgressText, kProgressTrackRect})
                    CHECK(SamePixels(off.dc(), on.dc(), ScaleRect(rect, scale, dpi)));
                CHECK(!SamePixels(off.dc(), on.dc(), ScaleRect(kDeductLunchToggle, scale, dpi)));
            }
            // The restored label fits the historical row at every supported scale/DPI.
            RECT label = ScaleRect(kDeductLunchLabel, scale, dpi);
            RECT measured = label;
            const auto oldFont = SelectObject(on.dc(), fonts.tiny);
            CHECK(DrawTextW(on.dc(), L"距离下班时间扣除午休", -1, &measured,
                DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX) > 0);
            CHECK(measured.right - measured.left <= label.right - label.left);
            CHECK(measured.bottom - measured.top <= label.bottom - label.top);
            SelectObject(on.dc(), oldFont);
            // Check the actual rendered label too, including encoding corruption.
            const auto brush = CreateSolidBrush(kPanelBackground);
            FillRect(off.dc(), &label, brush);
            DeleteObject(brush);
            const auto expectedFont = SelectObject(off.dc(), fonts.tiny);
            SetBkMode(off.dc(), TRANSPARENT);
            SetTextColor(off.dc(), kTextPrimary);
            RECT expectedLabel = label;
            DrawTextW(off.dc(), L"距离下班时间扣除午休", -1, &expectedLabel,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(off.dc(), expectedFont);
            CHECK(SamePixels(off.dc(), on.dc(), label));
            DestroyFonts(fonts);
        }
    }
}

void DurationOcrPreservesNetHours() {
    using namespace offwork;
    g.settings = Settings{};
    g.settings.lunchEndMinutes = 780;
    PopulateFields();
    CompleteOcr({{}, 535, {}});
    CHECK(g.settings.endMinutes == 1075 && g.fields[1].text == L"8");
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1075);
    g.settings.startMinutes = 1320;
    g.settings.endMinutes = 360;
    g.settings.lunchStartMinutes = 1410;
    g.settings.lunchEndMinutes = 30;
    PopulateFields();
    CHECK(g.fields[1].text == L"7");
    CompleteOcr({{}, 1315, {}});
    CHECK(g.settings.endMinutes == 355 && g.fields[1].text == L"7");
    CHECK(LoadSettings(g.settingsPath).endMinutes == 355);
    g.fields[4] = MakeField(L"01:00");
    ApplyLiveLunchChange();
    CHECK(g.settings.endMinutes == 385 && g.fields[1].text == L"7");
}

void DisplayToggleSavesWithoutChangingScheduleOrReminder() {
    using namespace offwork;
    g.settings = Settings{};
    PopulateFields();
    g.reminder = ResetReminder(WorkdayPhase::Working, 301);
    ToggleDeductLunch();
    CHECK(g.settings.deductLunch && LoadSettings(g.settingsPath).deductLunch);
    CHECK(BuildModel().settings.deductLunch);
    CHECK(g.settings.endMinutes == 1080 && g.fields[1].text == L"7.5");
    CHECK(g.reminder.lastRemainingSeconds == 301 && !g.reminder.fiveMinutesHandled);
    // Display-only settings must not suppress the next real 5-minute reminder.
    CHECK(TickReminder(g.reminder, WorkdayPhase::Working, 0, 300).notifyFiveMinutes);
    ToggleDeductLunch();
    CHECK(!g.settings.deductLunch && !LoadSettings(g.settingsPath).deductLunch);
    CHECK(g.reminder.lastRemainingSeconds == 301);
}

void OcrSilentlyUpdatesAndSavesTime() {
    using namespace offwork;
    const auto path = std::filesystem::temp_directory_path() /
        (L"offwork-ocr-edit-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    g.settingsPath = path.wstring();
    g.settings = Settings{};
    g.settings.endInputMode = EndInputMode::EndTime;
    PopulateFields();
    CompleteOcr({L"上班打卡 08:55", 535, {}});
    CHECK(g.settings.startMinutes == 535 && g.settings.endMinutes == 1080);
    CHECK(g.fields[0].text == L"08:55");
    CHECK(LoadSettings(g.settingsPath).startMinutes == 535);
    CHECK(g.footerText == kDefaultFooter && !g.footerIsError);

    g.settings = Settings{};
    CHECK(g.settings.endInputMode == EndInputMode::WorkDuration);
    PopulateFields();
    CHECK(g.editEndInputMode == EndInputMode::WorkDuration && g.fields[1].text == L"7.5");
    CompleteOcr({{}, 535, {}});
    CHECK(g.settings.startMinutes == 535 && g.settings.endMinutes == 1075);
    CHECK(g.fields[1].text == L"7.5");
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1075);

    g.expanded = false;
    CompleteOcr({{}, {}, L"unrecognized screenshot"});
    CHECK(g.settings.startMinutes == 535);
    CHECK(g.expanded && g.footerIsError);
    CHECK(BuildModel().footerText == L"unrecognized screenshot");
    CHECK(LoadSettings(g.settingsPath).startMinutes == 535);

    g.settings.endInputMode = EndInputMode::EndTime;
    CompleteOcr({{}, g.settings.endMinutes, {}}); // Conflicting schedule.
    CHECK(g.settings.startMinutes == 535 && g.footerIsError);
    CHECK(g.footerText.find(L"排班冲突") != std::wstring::npos);
    std::promise<OcrResult> failed;
    g.ocrTask = failed.get_future();
    failed.set_exception(std::make_exception_ptr(std::runtime_error("OCR worker")));
    PollScreenshotOcr();
    CHECK(!g.ocrTask.valid() && g.settings.startMinutes == 535);
    CHECK(g.footerIsError && g.footerText.find(L"识别失败") != std::wstring::npos);
    std::filesystem::remove(path);
}

// Real Windows OCR, async completion and presentation data: no injected result.
void ScreenshotOcrFillsTheUi(const std::wstring& image) {
    using namespace offwork;
    for (const bool expanded : {false, true}) {
        for (const auto mode : {EndInputMode::EndTime, EndInputMode::WorkDuration}) {
            g.settings = Settings{};
            g.settings.startMinutes = 532; // User's 08:52 / 18:50 schedule.
            g.settings.endMinutes = 1130;
            g.settings.lunchStartMinutes = 720;
            g.settings.lunchEndMinutes = 780;
            g.settings.endInputMode = mode;
            g.expanded = expanded;
            g.focusedField = -1;
            PopulateFields();
            BeginScreenshotOcr(image);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (g.ocrTask.valid() && std::chrono::steady_clock::now() < deadline) {
                PollScreenshotOcr();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            CHECK(!g.ocrTask.valid());
            const auto model = BuildModel();
            const auto saved = LoadSettings(g.settingsPath);
            const int end = mode == EndInputMode::EndTime ? 1130 : 1133;
            CHECK(model.startField.text == L"08:55");
            CHECK(model.settings.startMinutes == 535 && saved.startMinutes == 535);
            CHECK(model.settings.endMinutes == end && saved.endMinutes == end);
            CHECK(model.endField.text == (mode == EndInputMode::EndTime ? L"18:50" : L"8.97"));
            CHECK(g.expanded == expanded && g.focusedField == -1);
            CHECK(!model.footerIsError);
            const auto snapshot = CalculateWorkday(10 * 3600, model.settings.startMinutes,
                model.settings.endMinutes, model.settings.dailySalary, 720, 780);
            CHECK(snapshot.remainingSeconds == (mode == EndInputMode::EndTime ? 31800 : 31980));
        }
    }
    // A missing language pack, a rejected image and parse errors retain their
    // actionable reason even when the shell cannot display a tray balloon.
    for (const auto* reason : {L"Windows 未安装可用的文字识别语言，请在系统语言设置中安装简体中文 OCR。",
                              L"截图尺寸过大，请裁剪打卡结果后重试。",
                              L"未找到有效的上班打卡时间，请拖入包含打卡成功时间的清晰截图。"}) {
        g.expanded = false;
        CompleteOcr({{}, {}, reason});
        CHECK(g.expanded && BuildModel().footerIsError);
        CHECK(BuildModel().footerText == reason);
        CHECK(g.settings.startMinutes == 535 && LoadSettings(g.settingsPath).startMinutes == 535);
    }
    CompleteOcr({{}, {}, {}});
    CHECK(g.footerIsError && !g.footerText.empty());
    std::promise<OcrResult> pending;
    g.ocrTask = pending.get_future();
    BeginScreenshotOcr(image);
    CHECK(g.footerIsError && g.footerText.find(L"正在识别") != std::wstring::npos);
    pending.set_value({{}, 535, {}});
    PollScreenshotOcr();
    CHECK(!g.footerIsError);

    // Version comes from CMake and returns after a transient footer message.
    CHECK(BuildModel().footerText.find(OFFWORK_VERSION_W) != std::wstring::npos);
    SetFooter(L"temporary hint", true);
    CHECK(BuildModel().footerText == L"temporary hint");
    ResetFooter();
    CHECK(BuildModel().footerText.find(OFFWORK_VERSION_W) != std::wstring::npos);
}

void OcrErrorAndVersionFitInTheFooter() {
    using namespace offwork;
    const auto reason = L"无法识别截图，请确认文件可读取且图片完整，并在 Windows 语言设置中安装简体中文 OCR 后重试。";
    for (const int scale : {75, 100, 150}) {
        for (const UINT dpi : {96u, 144u, 192u}) {
            RenderModel model;
            model.expanded = true;
            model.settings.hideEarned = true;
            model.settings.uiScalePercent = scale;
            model.dpi = dpi;
            auto fonts = CreateFonts(scale, dpi);
            model.fonts = &fonts;
            const auto size = WindowSizeFor(scale, dpi, true);
            LayeredSurface empty, rendered;
            CHECK(empty.EnsureSize(size.cx, size.cy) && rendered.EnsureSize(size.cx, size.cy));
            RenderFrame(empty.dc(), model);
            const auto oldFont = SelectObject(rendered.dc(), fonts.tiny);
            RECT measured = ScaleRect(kFooterText, scale, dpi);
            CHECK(DrawTextW(rendered.dc(), kDefaultFooter, -1, &measured,
                            DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT) > 0);
            const auto normal = ScaleRect(kFooterText, scale, dpi);
            CHECK(measured.right - measured.left <= normal.right - normal.left);
            model.footerText = kDefaultFooter;
            RenderFrame(rendered.dc(), model);
            CHECK(!SamePixels(empty.dc(), rendered.dc(), normal));

            measured = ScaleRect(kFooterErrorText, scale, dpi);
            const int height = DrawTextW(rendered.dc(), reason, -1, &measured,
                DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            const auto error = ScaleRect(kFooterErrorText, scale, dpi);
            CHECK(height > normal.bottom - normal.top); // Needs multiple lines.
            CHECK(height <= error.bottom - error.top);
            model.footerText = reason;
            model.footerIsError = true;
            RenderFrame(rendered.dc(), model);
            CHECK(!SamePixels(empty.dc(), rendered.dc(), error));
            SelectObject(rendered.dc(), oldFont);
            DestroyFonts(fonts);
        }
    }
}

void VersionIsVisibleBelowCollapsedSettings() {
    using namespace offwork;
    const auto versionBox = kCollapsedVersionText;
    for (const int scale : {75, 100, 150}) {
        for (const UINT dpi : {96u, 144u, 192u}) {
            RenderModel model;
            model.settings.hideEarned = true;
            model.settings.uiScalePercent = scale;
            model.dpi = dpi;
            auto fonts = CreateFonts(scale, dpi);
            model.fonts = &fonts;
            const auto size = WindowSizeFor(scale, dpi, false);
            LayeredSurface expected, rendered;
            CHECK(expected.EnsureSize(size.cx, size.cy) && rendered.EnsureSize(size.cx, size.cy));
            const auto box = ScaleRect(versionBox, scale, dpi);
            CHECK(box.top >= ScaleRect(kSettingsButton, scale, dpi).bottom);
            CHECK(box.right <= size.cx && box.bottom <= size.cy);
            HBRUSH brush = CreateSolidBrush(kBackground);
            FillRect(expected.dc(), &box, brush);
            DeleteObject(brush);
            const auto oldFont = SelectObject(expected.dc(), fonts.tiny);
            SetBkMode(expected.dc(), TRANSPARENT);
            SetTextColor(expected.dc(), kTextMuted);
            RECT measured = box;
            const int height = DrawTextW(expected.dc(), L"v" OFFWORK_VERSION_W, -1, &measured,
                DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            CHECK(height > 0 && height <= box.bottom - box.top);
            CHECK(measured.right - measured.left <= box.right - box.left);
            RECT textBox = box;
            DrawTextW(expected.dc(), L"v" OFFWORK_VERSION_W, -1, &textBox,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(expected.dc(), oldFont);
            for (const auto* footer : {L"", L"temporary hint"}) {
                model.footerText = footer;
                RenderFrame(rendered.dc(), model);
                CHECK(SamePixels(expected.dc(), rendered.dc(), box));
            }
            DestroyFonts(fonts);
        }
    }
}

void EndTimeEditsApplyBeforeOcrRepopulatesFields() {
    using namespace offwork;
    g.settings = Settings{};
    g.settings.endInputMode = EndInputMode::EndTime;
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
    CHECK(g.settings.endMinutes == 1132 && LoadSettings(g.settingsPath).endMinutes == 1132);
    CHECK(g.fields[1].text == L"8.5" && g.fields[1].caret == 3);
    CompleteOcr({{}, 535, {}});
    CHECK(g.settings.endMinutes == 1135 && g.fields[1].text == L"8.5");
    CHECK(BuildModel().settings.endMinutes == 1135);
    CHECK(LoadSettings(g.settingsPath).endMinutes == 1135);

    SetFocusedField(1);
    CHECK(HandleFieldKey(VK_BACK)); // 8. is incomplete; preserve 8.5 hours.
    CHECK(g.fields[1].text == L"8." && g.settings.endMinutes == 1135);
    CHECK(HandleFieldKey(VK_BACK)); // 8 is complete and valid.
    CHECK(g.settings.endMinutes == 1105 && LoadSettings(g.settingsPath).endMinutes == 1105);
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

void ResignationEditorPersistsIndependently() {
    using namespace offwork;
    g.settings = Settings{};
    g.expanded = true;
    g.focusedField = -1;
    PopulateFields();
    CHECK(FilterFor(5) == FieldFilter::Date && MaxLengthFor(5) == 10);
    SetFocusedField(5);
    g.fields[5] = InsertText(SelectAll(g.fields[5]), L"2026-11-30", FilterFor(5), MaxLengthFor(5));
    CHECK(g.fields[5].text == L"2026-11-30");
    SetFocusedField(3); // Must not mistake the new field for a lunch endpoint.
    CHECK(g.settings.resignationDate == L"2026-11-30");
    CHECK(LoadSettings(g.settingsPath).resignationDate == L"2026-11-30");
    CHECK(!g.settings.showResignation);
    CHECK(g.settings.lunchStartMinutes == 720 && g.settings.lunchEndMinutes == 810);
    const auto reminder = g.reminder;
    ToggleShowResignation();
    CHECK(LoadSettings(g.settingsPath).showResignation);
    CHECK(g.reminder.lastPhase == reminder.lastPhase);
    ToggleShowResignation();
    CHECK(!LoadSettings(g.settingsPath).showResignation);
    CHECK(LoadSettings(g.settingsPath).resignationDate == L"2026-11-30");
    SetFocusedField(5);
    g.fields[5] = MakeField(L"2026-02-29");
    SetFocusedField(-1);
    CHECK(g.footerIsError && g.fields[5].text == L"2026-11-30");
    CHECK(LoadSettings(g.settingsPath).resignationDate == L"2026-11-30");
    SetFocusedField(5);
    g.fields[5] = MakeField(L"");
    SetFocusedField(-1);
    CHECK(!g.footerIsError && LoadSettings(g.settingsPath).resignationDate.empty());
    CHECK(g.settings.endMinutes == 1080 && g.settings.lunchEndMinutes == 810);
}

void SavePreview(offwork::LayeredSurface& surface, const std::filesystem::path& path) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = surface.width();
    info.bmiHeader.biHeight = -surface.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<char> pixels(static_cast<std::size_t>(surface.width()) * surface.height() * 4);
    CHECK(GetDIBits(surface.dc(), static_cast<HBITMAP>(GetCurrentObject(surface.dc(), OBJ_BITMAP)),
        0, surface.height(), pixels.data(), &info, DIB_RGB_COLORS) != 0);
    BITMAPFILEHEADER header = {};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(pixels.data(), static_cast<std::streamsize>(pixels.size()));
    CHECK(file.good());
}

void ResignationRendersAcrossLocalMidnight(const std::filesystem::path& previews = {}) {
    using namespace offwork;
    for (const int scale : {75, 100, 150}) {
        for (const UINT dpi : {96u, 144u, 192u}) {
            for (const bool expanded : {false, true}) {
                RenderModel m;
                m.expanded = expanded;
                m.settings.uiScalePercent = scale;
                m.settings.resignationDate = L"2026-11-30";
                m.startField = MakeField(L"09:00");
                m.endField = MakeField(L"7.5");
                m.salaryField = MakeField(L"500");
                m.lunchStartField = MakeField(L"12:00");
                m.lunchEndField = MakeField(L"13:30");
                m.footerText = kDefaultFooter;
                m.resignationDateField = MakeField(m.settings.resignationDate);
                m.dpi = dpi;
                m.now = {2026, 11, 4, 26, 23, 59, 59, 0};
                auto fonts = CreateFonts(scale, dpi);
                m.fonts = &fonts;
                const auto size = WindowSizeFor(scale, dpi, expanded);
                LayeredSurface hidden, shown, expected;
                CHECK(hidden.EnsureSize(size.cx, size.cy) && shown.EnsureSize(size.cx, size.cy));
                CHECK(expected.EnsureSize(size.cx, size.cy));
                const auto box = ScaleRect(kResignationText, scale, dpi);
                const auto expectText = [&](const wchar_t* text) {
                    HBRUSH brush = CreateSolidBrush(kBackground);
                    FillRect(expected.dc(), &box, brush);
                    DeleteObject(brush);
                    SetBkMode(expected.dc(), TRANSPARENT);
                    SetTextColor(expected.dc(), kTextMuted);
                    const auto old = SelectObject(expected.dc(), fonts.tiny);
                    RECT measured = box;
                    DrawTextW(expected.dc(), text, -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                    CHECK(measured.right - measured.left <= box.right - box.left);
                    CHECK(measured.bottom - measured.top <= box.bottom - box.top);
                    RECT target = box;
                    DrawTextW(expected.dc(), text, -1, &target,
                        DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SelectObject(expected.dc(), old);
                    CHECK(SamePixels(expected.dc(), shown.dc(), box));
                };
                RenderFrame(hidden.dc(), m);
                m.settings.showResignation = true;
                RenderFrame(shown.dc(), m);
                CHECK(!SamePixels(hidden.dc(), shown.dc(), box));
                expectText(L"剩余 2 天离职");
                CHECK(SamePixels(hidden.dc(), shown.dc(), ScaleRect(kCountdownText, scale, dpi)));
                if (expanded) {
                    const auto dateBox = ScaleRect(kResignationDateBox, scale, dpi);
                    CHECK(HitTest({dateBox.left + 2, dateBox.top + 2}, m) == HotElement::ResignationDateField);
                    const auto toggle = ScaleRect(kResignationHit, scale, dpi);
                    CHECK(HitTest({toggle.left + 2, toggle.top + 2}, m) == HotElement::ShowResignation);
                    CHECK(dateBox.bottom < ScaleRect(kResignationHit, scale, dpi).top);
                    CHECK(ScaleRect(kFooterErrorText, scale, dpi).bottom <= size.cy);
                }
                if (!previews.empty() && scale == 100 && dpi == 96) {
                    SavePreview(shown, previews / (expanded ? L"expanded.bmp" : L"collapsed.bmp"));
                    SavePreview(hidden, previews / (expanded ? L"expanded-hidden.bmp" : L"collapsed-hidden.bmp"));
                }
                m.now.wDay = 27; m.now.wHour = 0; m.now.wMinute = 0; m.now.wSecond = 0;
                RenderFrame(shown.dc(), m);
                expectText(L"剩余 1 天离职");
                m.now.wDay = 30;
                RenderFrame(shown.dc(), m);
                expectText(L"剩余 0 天离职");
                m.now.wMonth = 12; m.now.wDay = 1;
                RenderFrame(shown.dc(), m);
                expectText(L"");
                m.now.wMonth = 11; m.now.wDay = 26;
                for (const auto date : {L"", L"2026-02-29"}) {
                    m.settings.resignationDate = date;
                    RenderFrame(shown.dc(), m);
                    expectText(L"");
                }
                DestroyFonts(fonts);
            }
        }
    }
}

}  // namespace

void CalendarWindowChecks(const std::filesystem::path& previews = {}) {
    using namespace offwork;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW host{};
    host.lpfnWndProc = DefWindowProcW;
    host.hInstance = GetModuleHandleW(nullptr);
    host.lpszClassName = L"OffWork.CalendarTestHost";
    CHECK(RegisterClassW(&host) != 0);
    g.window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, host.lpszClassName,
        L"OffWork isolated calendar test", WS_POPUP | WS_VISIBLE, 20, 20, 360, 770,
        nullptr, nullptr, host.hInstance, nullptr);
    CHECK(g.window != nullptr);
    const auto pump = [] {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    };
    const auto open = [&] {
        ShowResignationCalendar();
        pump();
        const HWND window = FindWindowW(L"OffWork.ResignationCalendar", nullptr);
        CHECK(window != nullptr && GetWindow(window, GW_OWNER) == g.window);
        const HWND cal = FindWindowExW(window, nullptr, MONTHCAL_CLASSW, nullptr);
        CHECK(cal != nullptr && GetFocus() == cal);
        return std::pair{window, cal};
    };
    const auto selected = [](HWND cal) {
        SYSTEMTIME result{};
        CHECK(MonthCal_GetCurSel(cal, &result));
        return result;
    };
    g.settings = Settings{};
    g.settings.resignationDate = L"2027-03-15";
    g.settings.showResignation = true;
    g.expanded = true;
    PopulateFields();
    SaveSettings(g.settingsPath, g.settings);
    g.dpi = GetDpiForWindow(g.window);
    Present();
    std::cout << "Real HWND calendar; actual owner DPI=" << g.dpi << '\n';
    auto [window, cal] = open();
    CHECK(selected(cal).wYear == 2027 && selected(cal).wMonth == 3 && selected(cal).wDay == 15);
    CHECK(MonthCal_GetColor(cal, MCSC_MONTHBK) == kPanelBackground);
    CHECK(MonthCal_GetColor(cal, MCSC_TEXT) == kTextPrimary);
    if (!previews.empty()) {
        std::filesystem::create_directories(previews);
        SavePreview(g.surface, previews / L"expanded-calendar-entry.bmp");
        RECT bounds{};
        CHECK(GetWindowRect(window, &bounds));
        LayeredSurface snapshot;
        CHECK(snapshot.EnsureSize(bounds.right - bounds.left, bounds.bottom - bounds.top));
        CHECK(PrintWindow(window, snapshot.dc(), 0));
        SavePreview(snapshot, previews / L"calendar-window.bmp");
    }
    ShowResignationCalendar(); // Reopening reuses the owned popup.
    CHECK(FindWindowW(L"OffWork.ResignationCalendar", nullptr) == window);
    RECT navigationClient{};
    GetClientRect(cal, &navigationClient);
    bool nextClicked = false;
    for (int y = 0; y < navigationClient.bottom / 3 && !nextClicked; ++y) {
        for (int x = 0; x < navigationClient.right && !nextClicked; ++x) {
            MCHITTESTINFO hit{};
            hit.cbSize = MCHITTESTINFO_V1_SIZE;
            hit.pt = {x, y};
            if (MonthCal_HitTest(cal, &hit) != MCHT_TITLEBTNNEXT) continue;
            SendMessageW(cal, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(cal, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            nextClicked = true;
        }
    }
    CHECK(nextClicked);
    SYSTEMTIME visible[2]{};
    CHECK(MonthCal_GetMonthRange(cal, GMR_VISIBLE, visible) != 0);
    CHECK(visible[0].wMonth == 4 || visible[1].wMonth == 4);
    CHECK(LoadSettings(g.settingsPath).resignationDate == L"2027-03-15");
    SendMessageW(cal, WM_KEYDOWN, VK_NEXT, 0);
    CHECK(selected(cal).wMonth == 4 || selected(cal).wMonth == 5);
    CHECK(LoadSettings(g.settingsPath).resignationDate == L"2027-03-15");
    SendMessageW(cal, WM_KEYDOWN, VK_ESCAPE, 0);
    pump();
    CHECK(!IsWindow(window) && g.focusedField == 5 && GetFocus() == g.window);
    CHECK(g.settings.resignationDate == L"2027-03-15");
    std::tie(window, cal) = open();
    SendMessageW(cal, WM_KEYDOWN, VK_RIGHT, 0);
    CHECK(selected(cal).wDay == 16);
    SendMessageW(cal, WM_KEYDOWN, VK_RETURN, 0);
    pump();
    CHECK(!IsWindow(window) && LoadSettings(g.settingsPath).resignationDate == L"2027-03-16");
    CHECK(BuildModel().settings.resignationDate == L"2027-03-16");
    std::tie(window, cal) = open();
    // Use native hit testing and actual mouse messages to choose a day.
    RECT client{};
    GetClientRect(cal, &client);
    bool clicked = false;
    for (int y = 0; y < client.bottom && !clicked; y += 3) {
        for (int x = 0; x < client.right && !clicked; x += 3) {
            MCHITTESTINFO hit{};
            hit.cbSize = MCHITTESTINFO_V1_SIZE;
            hit.pt = {x, y};
            if (MonthCal_HitTest(cal, &hit) == MCHT_CALENDARDATE && hit.st.wDay == 20) {
                const LPARAM position = MAKELPARAM(x, y);
                SendMessageW(cal, WM_LBUTTONDOWN, MK_LBUTTON, position);
                SendMessageW(cal, WM_LBUTTONUP, 0, position);
                clicked = true;
            }
        }
    }
    CHECK(clicked);
    pump();
    CHECK(!IsWindow(window) && LoadSettings(g.settingsPath).resignationDate == L"2027-03-20");
    std::tie(window, cal) = open();
    SendMessageW(cal, WM_KEYDOWN, VK_TAB, 0);
    CHECK(GetFocus() == GetDlgItem(window, 100));
    SendMessageW(GetFocus(), WM_KEYDOWN, VK_RETURN, 0);
    pump();
    CHECK(!IsWindow(window) && LoadSettings(g.settingsPath).resignationDate.empty());
    CHECK(g.settings.showResignation && LoadSettings(g.settingsPath).showResignation);
    SYSTEMTIME today{};
    GetLocalTime(&today);
    std::tie(window, cal) = open();
    CHECK(selected(cal).wYear == today.wYear && selected(cal).wMonth == today.wMonth && selected(cal).wDay == today.wDay);
    // External activation cancels; draft navigation must not save.
    SendMessageW(cal, WM_KEYDOWN, VK_NEXT, 0);
    SendMessageW(window, WM_ACTIVATE, WA_INACTIVE, 0);
    CHECK(!IsWindow(window) && g.settings.resignationDate.empty());
    g.settings.showResignation = false;
    g.fields[5] = MakeField(L"2028-02-29");
    SetFocusedField(5);
    std::tie(window, cal) = open();
    CHECK(selected(cal).wYear == 2028 && selected(cal).wMonth == 2 && selected(cal).wDay == 29);
    CHECK(LoadSettings(g.settingsPath).resignationDate == L"2028-02-29");
    SendMessageW(cal, WM_KEYDOWN, VK_TAB, 0);
    SendMessageW(GetFocus(), WM_KEYDOWN, VK_TAB, 0);
    CHECK(GetFocus() == GetDlgItem(window, IDCANCEL));
    SendMessageW(GetFocus(), WM_KEYDOWN, VK_RETURN, 0);
    pump();
    CHECK(!IsWindow(window) && !LoadSettings(g.settingsPath).showResignation);
    CHECK(LoadSettings(g.settingsPath).resignationDate == L"2028-02-29");
    g.settings.resignationDate.clear();
    PopulateFields();
    MONITORINFO monitor{sizeof(monitor)};
    CHECK(GetMonitorInfoW(MonitorFromWindow(g.window, MONITOR_DEFAULTTONEAREST), &monitor));
    for (const UINT dpi : {96u, 144u, 192u}) {
        for (const int scale : {75, 100, 150}) {
            g.dpi = dpi;
            g.settings.uiScalePercent = scale;
            const SIZE size = WindowSizeFor(scale, dpi, true);
            for (const POINT corner : {POINT{monitor.rcWork.left, monitor.rcWork.top},
                POINT{monitor.rcWork.right - size.cx, monitor.rcWork.bottom - size.cy}}) {
                SetWindowPos(g.window, nullptr, corner.x, corner.y, size.cx, size.cy, SWP_NOZORDER);
                std::tie(window, cal) = open();
                RECT bounds{}, calendarBounds{}, clearBounds{};
                CHECK(GetWindowRect(window, &bounds) && GetWindowRect(cal, &calendarBounds));
                CHECK(GetWindowRect(GetDlgItem(window, 100), &clearBounds));
                CHECK(bounds.left >= monitor.rcWork.left && bounds.right <= monitor.rcWork.right);
                CHECK(bounds.top >= monitor.rcWork.top && bounds.bottom <= monitor.rcWork.bottom);
                CHECK(calendarBounds.bottom < clearBounds.top && clearBounds.bottom < bounds.bottom);
                const auto button = ScaleRect(kResignationCalendarButton, scale, dpi);
                CHECK(HitTest({button.left + 2, button.top + 2}, BuildModel()) == HotElement::ResignationCalendar);
                CloseResignationCalendar();
            }
        }
    }
    g.dpi = GetDpiForWindow(g.window);
    g.settings.uiScalePercent = 100;
    std::tie(window, cal) = open();
    RECT suggested{};
    SendMessageW(window, WM_DPICHANGED, MAKELONG(144, 144), reinterpret_cast<LPARAM>(&suggested));
    CHECK(!IsWindow(window) && g.settings.resignationDate.empty());
    CloseResignationCalendar();
    DestroyWindow(g.window);
    g.window = nullptr;
    DestroyFonts(g.fonts);
    UnregisterClassW(host.lpszClassName, host.hInstance);
    std::cout << "Calendar mouse selection, keyboard, focus, save/clear/cancel, and 18 DPI/scale/edge cases passed.\n";
}

int main(int argc, char** argv) {
    using namespace offwork;
    CHECK(g.editEndInputMode == EndInputMode::WorkDuration);
    CHECK(RenderModel{}.editEndInputMode == EndInputMode::WorkDuration);
    VersionIsVisibleBelowCollapsedSettings();
    const auto settingsPath = std::filesystem::temp_directory_path() /
        (L"offwork-live-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    g.settingsPath = settingsPath.wstring();
    if ((argc == 2 || argc == 3) && std::string(argv[1]) == "--calendar") {
        CalendarWindowChecks(argc == 3 ? std::filesystem::path{argv[2]} : std::filesystem::path{});
        std::filesystem::remove(settingsPath);
        return 0;
    }
    if ((argc == 2 && std::string(argv[1]) == "--resignation") ||
        (argc == 3 && std::string(argv[1]) == "--resignation-preview")) {
        if (argc == 3) std::filesystem::create_directories(argv[2]);
        ResignationEditorPersistsIndependently();
        ResignationRendersAcrossLocalMidnight(argc == 3 ? argv[2] : std::filesystem::path{});
        std::filesystem::remove(settingsPath);
        std::cout << "Resignation editing, rendering and local midnight checks passed.\n";
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--ocr-fill") {
        EndTimeEditsApplyBeforeOcrRepopulatesFields();
        OcrSilentlyUpdatesAndSavesTime();
        g.settingsPath = settingsPath.wstring();
        ScreenshotOcrFillsTheUi(std::filesystem::absolute(argv[2]).wstring());
        OcrErrorAndVersionFitInTheFooter();
        std::filesystem::remove(settingsPath);
        std::cout << "All OCR fill and version UI tests passed.\n";
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--ocr-endtime") {
        EndTimeEditsApplyBeforeOcrRepopulatesFields();
        OcrSilentlyUpdatesAndSavesTime();
        std::filesystem::remove(settingsPath);
        std::cout << "All OCR/end-time editor tests passed.\n";
        return 0;
    }
    NetDurationRecalculatesEnd();
    DurationOcrPreservesNetHours();
    DisplayToggleSavesWithoutChangingScheduleOrReminder();
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
    CHECK(g.fields[1].text == L"7.5");
    g.fields[1] = MakeField(L"8.5");
    CHECK(ReadFieldSettings(parsed) && parsed.endMinutes == 1140);
    SetEndInputMode(EndInputMode::EndTime);
    CHECK(g.fields[1].text == L"19:00");
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
    CHECK(g.settings.lunchEndMinutes == 780 && g.settings.endMinutes == 1140);
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
    CHECK(g.focusedField == 5);
    FocusNextField(1);
    CHECK(g.focusedField == 0);
    FocusNextField(-1);
    CHECK(g.focusedField == 5);
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
    OptionalLunchCountdownRendersOnlyBeforeLunchEnds();
    DropTargetRendersOnTheLayeredSurface();
    AllMoneyOutsideEditorIsMasked();
    SmallControlsAndMaskRenderCorrectly();
    std::filesystem::remove(settingsPath);
    std::cout << "All lunch editor and silent OCR tests passed.\n";
}
