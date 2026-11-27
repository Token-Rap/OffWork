//! GUI-free OffWork rules shared by every desktop shell.
//!
//! Each module is a port of the C++ app's pure logic, tested against the same
//! inputs and outputs as the old CTest suite (see `tests/`).
pub mod autostart;
pub mod edit;
pub mod image_file;
pub mod legacy;
pub mod money;
pub mod ocr;
pub mod placement;
pub mod reminder;
pub mod resignation;
pub mod schedule;
pub mod settings;
pub mod store;
pub mod update;

pub use schedule::Phase;
pub use settings::{EndMode, Settings};
