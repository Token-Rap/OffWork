#pragma once

// Colours and the logical layout grid.
//
// Every rectangle here is in *logical* units: the design is drawn against a
// 360-wide window at 100% scale and 96 DPI, and Scale()/ScaleForDpi() in the
// renderer map logical units onto device pixels. Keeping the grid in one place
// is what makes it possible to render a frame for a size the window does not
// have yet, which is how the resize stays coherent.

#include <windows.h>

namespace offwork {

struct LogicalRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// -- window metrics ---------------------------------------------------------

inline constexpr int kWindowWidth = 360;
inline constexpr int kCollapsedHeight = 330;
inline constexpr int kExpandedHeight = 660;
inline constexpr int kMinScalePercent = 75;
inline constexpr int kMaxScalePercent = 150;
inline constexpr int kMinOpacityPercent = 25;
inline constexpr int kCornerRadius = 8;

// -- colours ----------------------------------------------------------------

inline constexpr COLORREF kBackground = RGB(4, 17, 35);
inline constexpr COLORREF kTitleBackground = RGB(18, 29, 44);
inline constexpr COLORREF kPanelBackground = RGB(15, 29, 48);
inline constexpr COLORREF kInputBackground = RGB(27, 43, 65);
inline constexpr COLORREF kInputBorderFocused = RGB(79, 191, 244);
inline constexpr COLORREF kSelectionBackground = RGB(38, 84, 122);
inline constexpr COLORREF kButtonBackground = RGB(21, 35, 56);
inline constexpr COLORREF kButtonHover = RGB(32, 51, 77);
inline constexpr COLORREF kAccent = RGB(79, 191, 244);
inline constexpr COLORREF kAccentHover = RGB(102, 205, 250);
inline constexpr COLORREF kTextPrimary = RGB(244, 248, 253);
inline constexpr COLORREF kTextSecondary = RGB(188, 202, 220);
inline constexpr COLORREF kTextMuted = RGB(139, 156, 178);
inline constexpr COLORREF kDanger = RGB(255, 126, 142);
inline constexpr COLORREF kProgressTrack = RGB(135, 153, 176);
inline constexpr COLORREF kToggleOff = RGB(56, 72, 94);
inline constexpr COLORREF kEarned = RGB(127, 224, 250);
inline constexpr COLORREF kOnAccent = RGB(4, 34, 51);
inline constexpr COLORREF kDropBackground = RGB(10, 34, 52);

// -- title bar --------------------------------------------------------------

inline constexpr LogicalRect kTitleBar{0, 0, kWindowWidth, 56};
inline constexpr LogicalRect kIcon{16, 12, 48, 44};
inline constexpr LogicalRect kTitleText{56, 7, 220, 29};
inline constexpr LogicalRect kDateTimeText{56, 27, 235, 46};
inline constexpr LogicalRect kPinButton{272, 12, 304, 44};
inline constexpr LogicalRect kUpdateButton{240, 12, 266, 44};
inline constexpr LogicalRect kCloseButton{316, 12, 348, 44};

// -- summary ----------------------------------------------------------------

inline constexpr LogicalRect kStatusText{20, 68, 260, 88};
inline constexpr LogicalRect kCountdownText{20, 87, 340, 132};
inline constexpr LogicalRect kProgressTrackRect{20, 144, 340, 147};
inline constexpr LogicalRect kProgressText{180, 152, 340, 174};
inline constexpr LogicalRect kEarnedLabel{20, 184, 200, 204};
inline constexpr LogicalRect kEyeButton{134, 180, 162, 208};
inline constexpr LogicalRect kEarnedAmount{20, 204, 250, 244};
inline constexpr LogicalRect kSalaryText{220, 217, 340, 241};
inline constexpr LogicalRect kSettingsButton{20, 266, 340, 304};
inline constexpr LogicalRect kSettingsLabel{36, 266, 130, 304};
inline constexpr LogicalRect kCollapsedVersionText{250, 306, 340, 326};
inline constexpr int kChevronCenterX = 318;
inline constexpr int kChevronCenterY = 285;

// -- settings panel ---------------------------------------------------------
// Two-column fields keep the expanded panel within the existing height.
inline constexpr LogicalRect kPanel{20, 306, 340, 646};
inline constexpr LogicalRect kStartLabel{37, 313, 175, 333};
inline constexpr LogicalRect kStartBox{37, 334, 175, 366};
inline constexpr LogicalRect kSalaryLabel{191, 313, 323, 333};
inline constexpr LogicalRect kSalaryBox{191, 334, 323, 366};
inline constexpr LogicalRect kEndModeLabel{37, 372, 112, 393};
inline constexpr LogicalRect kEndTimeModeButton{112, 370, 212, 395};
inline constexpr LogicalRect kDurationModeButton{216, 370, 323, 395};
inline constexpr LogicalRect kEndBox{37, 397, 323, 429};
inline constexpr LogicalRect kLunchStartLabel{37, 433, 175, 453};
inline constexpr LogicalRect kLunchStartBox{37, 454, 175, 486};
inline constexpr LogicalRect kLunchEndLabel{191, 433, 323, 453};
inline constexpr LogicalRect kLunchEndBox{191, 454, 323, 486};
inline constexpr LogicalRect kDeductLunchLabel{37, 492, 275, 514};
inline constexpr LogicalRect kDeductLunchToggle{279, 492, 323, 514};
inline constexpr LogicalRect kDeductLunchHit{37, 488, 323, 518};
inline constexpr LogicalRect kOpacityLabel{37, 520, 323, 539};
inline constexpr LogicalRect kOpacityTrack{50, 546, 310, 552};
inline constexpr LogicalRect kOpacityHit{37, 536, 323, 564};
inline constexpr LogicalRect kAutostartLabel{37, 568, 240, 590};
inline constexpr LogicalRect kAutostartToggle{279, 568, 323, 590};
inline constexpr LogicalRect kAutostartHit{37, 564, 323, 594};
inline constexpr LogicalRect kFooterText{37, 630, 323, 644};
inline constexpr LogicalRect kFooterErrorText{37, 603, 323, 650};

// Horizontal padding between an input box edge and its text.
inline constexpr int kInputPadding = 10;

enum class HotElement {
    None,
    HideEarned,
    Update,
    Pin,
    Close,
    Settings,
    StartField,
    EndField,
    SalaryField,
    LunchStartField,
    LunchEndField,
    DeductLunch,
    EndTimeMode,
    DurationMode,
    Opacity,
    Autostart,
};

}  // namespace offwork
