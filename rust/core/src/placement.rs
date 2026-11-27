//! Window and popup geometry in physical pixels, ported from the C++ fixes:
//! the widget starts at the work-area top right and never leaves the work
//! area when it grows; the calendar opens below its anchor, flips above when
//! there is no room, clamps to the work area and scrolls when it cannot fit.

use serde::{Deserialize, Serialize};

pub const WIDTH: f64 = 360.0;
pub const COLLAPSED_HEIGHT: f64 = 384.0;
pub const EXPANDED_HEIGHT: f64 = 776.0;
pub const MARGIN: f64 = 16.0;

#[derive(Clone, Copy, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

impl Rect {
    pub fn right(&self) -> i32 {
        self.x + self.w
    }
    pub fn bottom(&self) -> i32 {
        self.y + self.h
    }
}

/// Logical window size for a scale percent.
pub fn window_size(scale_percent: i32, expanded: bool) -> (f64, f64) {
    let k = f64::from(scale_percent) / 100.0;
    let h = if expanded {
        EXPANDED_HEIGHT
    } else {
        COLLAPSED_HEIGHT
    };
    (WIDTH * k, h * k)
}

/// Logical window size for a scale percent that also fits the work area
/// (`work` in logical pixels). Height and width shrink to the work area; the
/// UI lays out against the real window height and scrolls the settings panel.
pub fn fitted_size(scale_percent: i32, expanded: bool, work: (f64, f64)) -> (f64, f64) {
    let (w, h) = window_size(scale_percent, expanded);
    (w.min(work.0).floor(), h.min(work.1).floor())
}

/// Top right of the work area, `margin` physical pixels in from the edges.
pub fn initial_position(work: Rect, size: (i32, i32), margin: i32) -> (i32, i32) {
    clamp_into(work, work.right() - size.0 - margin, work.y + margin, size)
}

/// Keeps a window of `size` inside `work`, preferring its top-left corner
/// when it is larger than the work area.
pub fn clamp_into(work: Rect, x: i32, y: i32, size: (i32, i32)) -> (i32, i32) {
    let x = x.min(work.right() - size.0).max(work.x);
    let y = y.min(work.bottom() - size.1).max(work.y);
    (x, y)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
pub struct Placement {
    pub rect: Rect,
    pub vertical_scroll: bool,
    pub horizontal_scroll: bool,
}

/// `ShowResignationCalendarAt`: `content` is the popup's natural size and
/// `bars` the vertical/horizontal scroll bar thickness.
pub fn popup(anchor: Rect, work: Rect, content: (i32, i32), bars: (i32, i32)) -> Option<Placement> {
    if work.w <= 0 || work.h <= 0 {
        return None;
    }
    let (cw, ch) = content;
    let (v_bar, h_bar) = bars;
    let mut vertical = ch > work.h;
    let horizontal = cw + if vertical { v_bar } else { 0 } > work.w;
    vertical = ch + if horizontal { h_bar } else { 0 } > work.h;
    let w = (cw + if vertical { v_bar } else { 0 }).min(work.w);
    let h = (ch + if horizontal { h_bar } else { 0 }).min(work.h);
    let x = anchor.x.clamp(work.x, work.x.max(work.right() - w));
    let preferred = if anchor.bottom() + h <= work.bottom() {
        anchor.bottom()
    } else {
        anchor.y - h
    };
    let y = preferred.clamp(work.y, work.y.max(work.bottom() - h));
    Some(Placement {
        rect: Rect { x, y, w, h },
        vertical_scroll: vertical,
        horizontal_scroll: horizontal,
    })
}
