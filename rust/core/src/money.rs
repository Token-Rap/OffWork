//! Port of `src/money_display.h`. Apply to every amount outside the editor.
pub const MASK: &str = "****";

pub fn display(formatted_amount: &str, hide: bool) -> String {
    if hide {
        MASK.to_owned()
    } else {
        formatted_amount.to_owned()
    }
}
