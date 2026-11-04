// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The organs the console offers: those the instrument carries, and every
//! `.organ.json` in the organs folder and beside the organ playing.
//!
//! The organs folder is `CAECILIA_ORGANS` if it is set, else
//! `%APPDATA%\Caecilia\Organs` (`~/.config/caecilia/organs` elsewhere).

use std::path::{Path, PathBuf};

/// An organ to choose: its name, and its file (none for one the instrument
/// carries).
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

/// The file the installer leaves in the organs folder, to copy and edit.
const INSTALLED_EXAMPLE: &str = "caecilia-pipe-organ.organ.json";

/// Whether `path` is that file as it was left: the organ first in the list.
fn is_untouched_example(path: &Path) -> bool {
    path.file_name().is_some_and(|n| n == INSTALLED_EXAMPLE)
        && std::fs::read_to_string(path)
            .ok()
            .and_then(|text| caecilia_model::read_organ(&text, None).ok())
            .is_some_and(|read| {
                caecilia_model::write_organ(&read.organ)
                    == caecilia_model::write_organ(&caecilia_model::caecilia_organ())
            })
}

/// The organs to offer, those the instrument carries first, then by name.
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
            let hidden = is_untouched_example(&path) && playing != Some(path.as_path());
            if is_organ_file(&path) && !hidden && !files.contains(&path) {
                files.push(path);
            }
        }
    }
    files.sort_by_key(|p| name_of(p).to_lowercase());
    let mut entries: Vec<Entry> = caecilia_model::BUILT_IN_ORGANS
        .iter()
        .map(|(name, _)| Entry {
            name: (*name).into(),
            path: None,
        })
        .collect();
    entries.extend(files.into_iter().map(|path| Entry {
        name: name_of(&path),
        path: Some(path),
    }));
    entries
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_installed_example_is_not_a_second_caecilia_organ_until_edited() {
        let shipped = Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../examples")
            .join(INSTALLED_EXAMPLE);
        assert!(is_untouched_example(&shipped));

        let dir = std::env::temp_dir().join(format!("caecilia-library-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let edited = dir.join(INSTALLED_EXAMPLE);
        let text = std::fs::read_to_string(&shipped).unwrap();
        std::fs::write(&edited, text.replacen("Montre 16", "Montre 16 bis", 1)).unwrap();
        assert!(!is_untouched_example(&edited));
        std::fs::remove_dir_all(&dir).unwrap();
    }
}
