//! The record: `install.json` in the program folder, exactly what an
//! installation made. Upgrades and the uninstaller work from it alone, so
//! nothing is removed that the installer did not place.

use std::path::{Path, PathBuf};

use serde_json::{Value, json};

use crate::payload::Digest;
use crate::plan::{Components, Scope};

/// The record's name in the program folder.
pub const RECORD: &str = "install.json";
/// The log's copy in the program folder.
pub const LOG_COPY: &str = "install.log";

/// A file the installation placed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RecordedFile {
    /// Where.
    pub path: PathBuf,
    /// Its length.
    pub size: u64,
    /// Its SHA-256 as placed: a file that no longer has it was changed
    /// since, and is kept.
    pub sha256: Digest,
}

/// What an installation made.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Record {
    /// The product.
    pub product: String,
    /// The version installed.
    pub version: String,
    /// Who it is for.
    pub scope: Scope,
    /// The components installed.
    pub components: Components,
    /// The program folder.
    pub program_dir: PathBuf,
    /// The organs folder it used.
    pub organs_dir: Option<PathBuf>,
    /// Every file placed, in order.
    pub files: Vec<RecordedFile>,
    /// Every folder made, parents first.
    pub dirs: Vec<PathBuf>,
    /// The shortcuts made.
    pub shortcuts: Vec<PathBuf>,
    /// Old files that were in use and could not be deleted yet; the next
    /// run deletes them.
    pub leftovers: Vec<PathBuf>,
    /// Whether the desktop and Start menu shortcuts were asked for (an
    /// upgrade asks again for what was).
    pub desktop: bool,
    /// See [`desktop`](Record::desktop).
    pub start_menu: bool,
}

impl Record {
    /// The record as JSON.
    pub fn to_json(&self) -> String {
        let paths = |v: &[PathBuf]| v.iter().map(|p| p.to_string_lossy().into_owned()).collect::<Vec<_>>();
        let files: Vec<Value> = self
            .files
            .iter()
            .map(|f| json!({ "path": f.path.to_string_lossy(), "size": f.size, "sha256": f.sha256.hex() }))
            .collect();
        let v = json!({
            "format": 1,
            "product": self.product,
            "version": self.version,
            "scope": self.scope.name(),
            "components": self.components.list(),
            "program_dir": self.program_dir.to_string_lossy(),
            "organs_dir": self.organs_dir.as_ref().map(|p| p.to_string_lossy().into_owned()),
            "desktop": self.desktop,
            "start_menu": self.start_menu,
            "files": files,
            "dirs": paths(&self.dirs),
            "shortcuts": paths(&self.shortcuts),
            "leftovers": paths(&self.leftovers),
        });
        serde_json::to_string_pretty(&v).unwrap_or_default()
    }

    /// The record `text` holds, if it is one.
    pub fn from_json(text: &str) -> Option<Record> {
        let v: Value = serde_json::from_str(text).ok()?;
        if v.get("format")?.as_u64()? != 1 {
            return None;
        }
        let s = |k: &str| v.get(k).and_then(Value::as_str);
        let paths = |k: &str| -> Option<Vec<PathBuf>> {
            v.get(k)?
                .as_array()?
                .iter()
                .map(|p| p.as_str().map(PathBuf::from))
                .collect()
        };
        let mut files = Vec::new();
        for f in v.get("files")?.as_array()? {
            files.push(RecordedFile {
                path: PathBuf::from(f.get("path")?.as_str()?),
                size: f.get("size")?.as_u64()?,
                sha256: Digest::from_hex(f.get("sha256")?.as_str()?)?,
            });
        }
        Some(Record {
            product: s("product")?.into(),
            version: s("version")?.into(),
            scope: Scope::from_name(s("scope")?)?,
            components: Components::parse(s("components")?)?,
            program_dir: PathBuf::from(s("program_dir")?),
            organs_dir: s("organs_dir").map(PathBuf::from),
            desktop: v.get("desktop").and_then(Value::as_bool).unwrap_or(true),
            start_menu: v.get("start_menu").and_then(Value::as_bool).unwrap_or(true),
            files,
            dirs: paths("dirs")?,
            shortcuts: paths("shortcuts")?,
            leftovers: paths("leftovers").unwrap_or_default(),
        })
    }

    /// The record in the program folder `dir`, if there is one.
    pub fn load(dir: &Path) -> Option<Record> {
        Record::from_json(&std::fs::read_to_string(dir.join(RECORD)).ok()?)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_record_reads_back() {
        let r = Record {
            product: "Caecilia".into(),
            version: "2.0.0".into(),
            scope: Scope::Machine,
            components: Components::parse("app,clap").unwrap(),
            program_dir: "C:\\Program Files\\Caecilia".into(),
            organs_dir: Some("C:\\Users\\a\\AppData\\Roaming\\Caecilia\\Organs".into()),
            files: vec![RecordedFile {
                path: "C:\\Program Files\\Caecilia\\caecilia.exe".into(),
                size: 3,
                sha256: Digest::of(b"app"),
            }],
            dirs: vec!["C:\\Program Files\\Caecilia".into()],
            shortcuts: vec!["C:\\Users\\Public\\Desktop\\Caecilia.lnk".into()],
            leftovers: vec![],
            desktop: true,
            start_menu: false,
        };
        assert_eq!(Record::from_json(&r.to_json()), Some(r));
        assert_eq!(Record::from_json("{}"), None);
    }
}
