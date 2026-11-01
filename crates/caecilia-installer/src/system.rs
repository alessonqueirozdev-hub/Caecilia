//! What an installation asks of the system beyond files: the entry in
//! *Apps & features*, shortcuts, deleting a file in use at the next
//! restart. Behind a trait, so tests run on a [`MemorySystem`] and never
//! touch the registry, the desktop or the Start menu.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use crate::plan::{Scope, Shortcut, UninstallEntry};

/// An installed version, as *Apps & features* knows it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Installed {
    /// Its version.
    pub version: String,
    /// Its program folder.
    pub location: PathBuf,
}

/// The system, beyond files.
pub trait System {
    /// The installation of `product` in `scope`, if there is one.
    fn installed(&self, scope: Scope, product: &str) -> Option<Installed>;
    /// Writes the entry in *Apps & features*.
    fn register(&mut self, scope: Scope, entry: &UninstallEntry) -> std::io::Result<()>;
    /// Removes the entry of `product`.
    fn unregister(&mut self, scope: Scope, product: &str) -> std::io::Result<()>;
    /// Makes a shortcut.
    fn make_shortcut(&mut self, shortcut: &Shortcut) -> std::io::Result<()>;
    /// Has the system delete `path` when it next starts; whether it will.
    fn delete_at_restart(&mut self, path: &Path) -> bool;
    /// Takes the one lock of `product`'s installers, held until the process
    /// ends, so two runs never work on the same files; false if another
    /// run holds it.
    fn lock(&mut self, product: &str) -> bool;
}

/// A system in memory, for tests: shortcuts are small files that say what
/// they open, entries live in a map.
#[derive(Debug, Default)]
pub struct MemorySystem {
    /// The entries, by scope and product.
    pub entries: HashMap<(Scope, String), UninstallEntry>,
    /// What was given to delete at restart.
    pub at_restart: Vec<PathBuf>,
    /// Whether deleting at restart is allowed, as for an administrator.
    pub can_delete_at_restart: bool,
    /// Fails [`register`](System::register), to test the way back.
    pub fail_register: bool,
    /// Fails [`unregister`](System::unregister).
    pub fail_unregister: bool,
    /// Another run holds the lock.
    pub locked_elsewhere: bool,
}

impl System for MemorySystem {
    fn installed(&self, scope: Scope, product: &str) -> Option<Installed> {
        self.entries
            .get(&(scope, product.to_owned()))
            .map(|e| Installed {
                version: e.version.clone(),
                location: e.location.clone(),
            })
    }

    fn register(&mut self, scope: Scope, entry: &UninstallEntry) -> std::io::Result<()> {
        if self.fail_register {
            return Err(std::io::Error::other("the registry said no"));
        }
        self.entries
            .insert((scope, entry.name.clone()), entry.clone());
        Ok(())
    }

    fn unregister(&mut self, scope: Scope, product: &str) -> std::io::Result<()> {
        if self.fail_unregister {
            return Err(std::io::Error::other("refused"));
        }
        self.entries.remove(&(scope, product.to_owned()));
        Ok(())
    }

    fn make_shortcut(&mut self, shortcut: &Shortcut) -> std::io::Result<()> {
        std::fs::write(&shortcut.link, shortcut.target.to_string_lossy().as_bytes())
    }

    fn delete_at_restart(&mut self, path: &Path) -> bool {
        if self.can_delete_at_restart {
            self.at_restart.push(path.to_owned());
        }
        self.can_delete_at_restart
    }

    fn lock(&mut self, _product: &str) -> bool {
        !self.locked_elsewhere
    }
}
