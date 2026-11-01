//! The wizard, drawn with `manguebyte-ui` in the console's materials: a
//! wooden jamb with the brass name plate and the steps, a paper panel by
//! day (leather by night) with the page, buttons of ivory and brass, the
//! progress as a wind gauge filling. See `docs/mockups/installer.html`.

pub mod look;
pub mod paint;
pub mod wizard;

use std::cell::Cell;
use std::rc::Rc;

use manguebyte_gui::{AppEditor, open_app_window};
use manguebyte_plugin::editor::EditorError;
use manguebyte_window::{App, AppWindowOptions};

pub use wizard::{Env, Facts, Page, SIZE, Wizard};

/// Opens the wizard in a window of `app`, titled `title`. The window
/// refuses to close while `busy` is set (work is running).
pub fn open(
    app: &App,
    title: &str,
    wizard: Wizard,
    dialogs: Rc<std::cell::RefCell<Option<manguebyte_window::Dialogs>>>,
    busy: Rc<Cell<bool>>,
) -> Result<AppEditor, EditorError> {
    let options = AppWindowOptions::new(title, SIZE).fixed();
    let editor = open_app_window(app, options, look::theme(&look::DAY), None, wizard)?;
    *dialogs.borrow_mut() = Some(editor.dialogs());
    editor.on_close_request(move |_| !busy.get());
    Ok(editor)
}
