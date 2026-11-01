//! `caecilia-pack`: builds the installer's payload from the built files.
//!
//! ```text
//! caecilia-pack --out caecilia.payload --product Caecilia --version 2.0.0
//!     --publisher "Alesson Queiroz"
//!     --file <component> <root> <path> <source>    one file
//!     --dir  <component> <root> <path> <source>    a folder, everything in it
//! ```
//!
//! `component` is `core` (always installed), `app`, `vst3`, `clap` or
//! `organ`; `root` is `program`, `vst3`, `clap` or `organs`; `path` is
//! where it goes under that root, with `/` between parts. See
//! `scripts/build-installer.ps1`.

use std::path::{Path, PathBuf};
use std::process::ExitCode;

use caecilia_installer::payload::{Payload, PayloadBuilder, Root};
use caecilia_installer::text::size;

fn main() -> ExitCode {
    match run(std::env::args().skip(1).collect()) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("caecilia-pack: {e}");
            ExitCode::FAILURE
        }
    }
}

fn run(args: Vec<String>) -> Result<(), String> {
    let mut out = None;
    let (mut product, mut version, mut publisher) = (None, None, None);
    let mut adds: Vec<(String, Root, String, PathBuf, bool)> = Vec::new();
    let mut it = args.into_iter();
    while let Some(arg) = it.next() {
        let mut next = |what: &str| it.next().ok_or(format!("{arg} needs {what}"));
        match arg.as_str() {
            "--out" => out = Some(PathBuf::from(next("a file")?)),
            "--product" => product = Some(next("a name")?),
            "--version" => version = Some(next("a version")?),
            "--publisher" => publisher = Some(next("a name")?),
            "--file" | "--dir" => {
                let component = next("a component")?;
                let root = next("a root")?;
                let root = Root::from_name(&root).ok_or(format!("unknown root '{root}'"))?;
                let path = next("a path")?;
                let source = PathBuf::from(next("a source")?);
                adds.push((component, root, path, source, arg == "--dir"));
            }
            other => return Err(format!("unknown argument '{other}'")),
        }
    }
    let out = out.ok_or("--out is needed")?;
    let mut builder = PayloadBuilder::new(
        &product.ok_or("--product is needed")?,
        &version.ok_or("--version is needed")?,
        &publisher.ok_or("--publisher is needed")?,
    );
    let mut count = 0;
    let mut total = 0;
    for (component, root, path, source, dir) in adds {
        let files = if dir {
            walk(&source)?
        } else {
            vec![(String::new(), source.clone())]
        };
        for (rel, file) in files {
            let bytes = std::fs::read(&file).map_err(|e| format!("{}: {e}", file.display()))?;
            let dest = if rel.is_empty() {
                path.clone()
            } else {
                format!("{path}/{rel}")
            };
            builder.add(&component, root, &dest, &bytes);
            count += 1;
            total += bytes.len() as u64;
            println!("  {component:<5} {:<7} {dest}", root.name());
        }
    }
    let distinct = builder.unpacked_size();
    let bytes = builder.build().map_err(|e| e.to_string())?;
    // What was written must read back.
    Payload::parse(&bytes)
        .and_then(|p| p.unpack())
        .map_err(|e| format!("the payload does not read back: {e}"))?;
    if let Some(dir) = out.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(&out, &bytes).map_err(|e| format!("{}: {e}", out.display()))?;
    println!(
        "{count} files, {} ({} distinct), packed into {}: {}",
        size(total),
        size(distinct),
        size(bytes.len() as u64),
        out.display()
    );
    Ok(())
}

/// Every file under `dir`, with its path from there (`/` between parts),
/// in a stable order.
fn walk(dir: &Path) -> Result<Vec<(String, PathBuf)>, String> {
    let mut out = Vec::new();
    let mut stack = vec![(String::new(), dir.to_owned())];
    while let Some((prefix, d)) = stack.pop() {
        let mut entries: Vec<_> = std::fs::read_dir(&d)
            .map_err(|e| format!("{}: {e}", d.display()))?
            .filter_map(Result::ok)
            .collect();
        entries.sort_by_key(|e| e.file_name());
        for e in entries {
            let name = e.file_name().to_string_lossy().into_owned();
            let rel = if prefix.is_empty() {
                name
            } else {
                format!("{prefix}/{name}")
            };
            if e.path().is_dir() {
                stack.push((rel, e.path()));
            } else {
                out.push((rel, e.path()));
            }
        }
    }
    out.sort();
    Ok(out)
}
