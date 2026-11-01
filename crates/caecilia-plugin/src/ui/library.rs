//! The organs the console offers: the demonstration organ, and every
//! `.organ.json` in the organs folder and beside the organ playing.
//!
//! The organs folder is `CAECILIA_ORGANS` if it is set, else
//! `%APPDATA%\Caecilia\Organs` (`~/.config/caecilia/organs` elsewhere).

use std::path::{Path, PathBuf};

/// An organ to choose: its name, and its file (none for the built-in one).
#[derive(Clone, PartialEq, Debug)]
pub(crate) struct Entry {
    pub(crate) name: String,
    pub(crate) path: Option<PathBuf>,
}

/// The environment variable naming the organs folder.
pub(crate) const ORGANS_ENV: &str = "CAECILIA_ORGANS";

/// Where organs are put to be found.
pub(crate) fn organs_folder() -> Option<PathBuf> {
    if let Some(dir) = std::env::var_os(ORGANS_ENV) {
        return Some(PathBuf::from(dir));
    }
    let base = std::env::var_os("APPDATA")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))?;
    Some(if cfg!(windows) {
        base.join("Caecilia").join("Organs")
    } else {
        base.join("caecilia").join("organs")
    })
}

fn is_organ_file(path: &Path) -> bool {
    path.file_name()
        .and_then(|n| n.to_str())
        .is_some_and(|n| n.to_ascii_lowercase().ends_with(".organ.json"))
}

/// An organ's name from its file: `St Aubin` for `St Aubin.organ.json`.
fn name_of(path: &Path) -> String {
    let file = path.file_name().and_then(|n| n.to_str()).unwrap_or("");
    let cut = file.len().saturating_sub(".organ.json".len());
    file.get(..cut).unwrap_or(file).to_string()
}

/// The organs to offer, the demonstration organ first, then by name.
pub(crate) fn scan(playing: Option<&Path>) -> Vec<Entry> {
    let mut folders: Vec<PathBuf> = organs_folder().into_iter().collect();
    if let Some(dir) = playing.and_then(Path::parent) {
        folders.push(dir.to_path_buf());
    }
    let mut files: Vec<PathBuf> = Vec::new();
    for folder in folders {
        let Ok(read) = std::fs::read_dir(&folder) else {
            continue;
        };
        for entry in read.flatten() {
            let path = std::path::absolute(entry.path()).unwrap_or_else(|_| entry.path());
            if is_organ_file(&path) && !files.contains(&path) {
                files.push(path);
            }
        }
    }
    files.sort_by_key(|p| name_of(p).to_lowercase());
    let mut entries = vec![Entry {
        name: "Demonstration organ".into(),
        path: None,
    }];
    entries.extend(files.into_iter().map(|path| Entry {
        name: name_of(&path),
        path: Some(path),
    }));
    entries
}
