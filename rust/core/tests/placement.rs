//! Window and calendar geometry, ported from the C++ calendar checks
//! (18 DPI/scale/corner cases plus synthetic small work areas).
use offwork_core::placement::*;

fn monitor() -> Rect {
    Rect {
        x: 0,
        y: 0,
        w: 1920,
        h: 1040,
    }
}

#[test]
fn calendar_stays_in_the_work_area_for_every_dpi_scale_and_corner() {
    for (work, label) in [
        (monitor(), "primary"),
        (
            Rect {
                x: -1280,
                y: -200,
                w: 1280,
                h: 984,
            },
            "negative secondary",
        ),
    ] {
        for dpi in [96, 144, 192] {
            for scale in [75, 100, 150] {
                let k = f64::from(dpi) / 96.0 * f64::from(scale) / 100.0;
                let px = |v: f64| (v * k).round() as i32;
                let (ww, wh) = window_size(scale, true);
                let dpr = f64::from(dpi) / 96.0;
                let size = ((ww * dpr).round() as i32, (wh * dpr).round() as i32);
                for (x, y) in [
                    (work.x, work.y),
                    (work.right() - size.0, work.bottom() - size.1),
                ] {
                    // The date box inside the expanded widget.
                    let anchor = Rect {
                        x: x + px(37.),
                        y: y + px(621.),
                        w: px(218.),
                        h: px(32.),
                    };
                    let p = popup(anchor, work, (px(280.), px(330.)), (px(17.), px(17.))).unwrap();
                    let r = p.rect;
                    assert!(
                        r.x >= work.x && r.right() <= work.right(),
                        "{label} {dpi} {scale} {r:?}"
                    );
                    assert!(
                        r.y >= work.y && r.bottom() <= work.bottom(),
                        "{label} {dpi} {scale} {r:?}"
                    );
                    // Opens below when there is room, otherwise flips above.
                    if anchor.bottom() + r.h <= work.bottom() {
                        assert_eq!(r.y, anchor.bottom());
                    } else {
                        assert!(r.bottom() <= anchor.y || r.y == work.y);
                    }
                }
            }
        }
    }
}

#[test]
fn small_work_areas_clamp_and_scroll() {
    let content = (280 * 3, 330 * 3); // 150 % at 192 DPI
    for height in [360, 96] {
        let work = Rect {
            x: 100,
            y: 50,
            w: 400,
            h: height,
        };
        let anchor = Rect {
            x: work.right() - 30,
            y: work.bottom() - 5,
            w: 30,
            h: 5,
        };
        let p = popup(anchor, work, content, (34, 34)).unwrap();
        assert!(p.rect.x >= work.x && p.rect.right() == work.right());
        assert!(p.rect.y >= work.y && p.rect.bottom() <= work.bottom());
        assert!(p.vertical_scroll && p.horizontal_scroll);
    }
    // Fits: no scroll bars, natural size, opens below.
    let p = popup(
        Rect {
            x: 10,
            y: 10,
            w: 100,
            h: 20,
        },
        monitor(),
        (280, 330),
        (17, 17),
    )
    .unwrap();
    assert_eq!(
        p.rect,
        Rect {
            x: 10,
            y: 30,
            w: 280,
            h: 330
        }
    );
    assert!(!p.vertical_scroll && !p.horizontal_scroll);
    // Only too tall: a vertical bar adds width but no horizontal bar.
    let p = popup(
        Rect {
            x: 0,
            y: 0,
            w: 10,
            h: 10,
        },
        Rect {
            x: 0,
            y: 0,
            w: 1000,
            h: 200,
        },
        (280, 330),
        (17, 17),
    )
    .unwrap();
    assert!(p.vertical_scroll && !p.horizontal_scroll && p.rect.w == 297 && p.rect.h == 200);
    assert!(popup(
        Rect::default(),
        Rect {
            x: 0,
            y: 0,
            w: 0,
            h: 10
        },
        (1, 1),
        (1, 1)
    )
    .is_none());
}

#[test]
fn widget_starts_top_right_and_stays_inside_when_expanding() {
    let work = Rect {
        x: 1920,
        y: 0,
        w: 2560,
        h: 1400,
    }; // second monitor
    let (x, y) = initial_position(work, (540, 576), 24);
    assert_eq!((x, y), (1920 + 2560 - 540 - 24, 24));
    // Expanding near the bottom moves the window up instead of off-screen.
    let (x, y) = clamp_into(work, x, 1000, (540, 1164));
    assert_eq!((x, y), (1920 + 2560 - 540 - 24, 1400 - 1164));
    // Larger than the work area: keep the top-left (title bar) reachable.
    assert_eq!(clamp_into(work, 5000, 5000, (3000, 2000)), (1920, 0));
    assert_eq!(window_size(150, true), (540.0, 1164.0));
    assert_eq!(window_size(75, false), (270.0, 288.0));
}

/// Real-machine report: at 1920x1080 / 96 DPI (work area 1920x1032), the
/// expanded widget was cut off at 125 % and overflowed by 132 px at 150 %.
#[test]
fn expanded_widget_fits_every_scale_on_1080p_and_smaller_work_areas() {
    for (work_w, work_h, dpr) in [
        (1920, 1032, 1.0),
        (1366, 728, 1.0),
        (1280, 600, 1.0),
        (2560, 1392, 2.0),
        (1024, 728, 1.25),
    ] {
        let work = Rect {
            x: 0,
            y: 0,
            w: work_w,
            h: work_h,
        };
        let logical = (f64::from(work_w) / dpr, f64::from(work_h) / dpr);
        for scale in [75, 100, 125, 150] {
            for expanded in [false, true] {
                let (lw, lh) = fitted_size(scale, expanded, logical);
                let natural = window_size(scale, expanded);
                assert!(
                    lw <= logical.0 && lh <= logical.1,
                    "{work_w}x{work_h} {scale}% {lw}x{lh}"
                );
                // Never shrinks when it already fits.
                if natural.1 <= logical.1 {
                    assert_eq!(lh, natural.1.floor(), "{work_w}x{work_h} {scale}%");
                }
                let size = ((lw * dpr).round() as i32, (lh * dpr).round() as i32);
                // Wherever the user left it, clamping keeps it fully inside.
                for (x, y) in [
                    (work.right() - size.0 - 16, 16),
                    (0, work.bottom() - 10),
                    (work.right(), work.bottom()),
                ] {
                    let (x, y) = clamp_into(work, x, y, size);
                    assert!(
                        x >= work.x && x + size.0 <= work.right(),
                        "{work_w}x{work_h} {scale}% x"
                    );
                    assert!(
                        y >= work.y && y + size.1 <= work.bottom(),
                        "{work_w}x{work_h} {scale}% y={y} h={}",
                        size.1
                    );
                }
            }
        }
    }
    // The reported cases: 125 % fits as is, 150 % is shortened to the work area.
    assert_eq!(fitted_size(125, true, (1920.0, 1032.0)), (450.0, 970.0));
    assert_eq!(fitted_size(150, true, (1920.0, 1032.0)), (540.0, 1032.0));
}
