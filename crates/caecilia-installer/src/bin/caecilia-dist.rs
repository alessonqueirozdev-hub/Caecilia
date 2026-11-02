//! `caecilia-dist`: builds Caecilia's Windows installer.
//!
//! ```text
//! cargo run --profile caecilia-release -p caecilia-installer --bin caecilia-dist -- build
//!     [--target-dir C:/manguebyte/target] [--manguebyte C:/manguebyte]
//!     [--offline] [--sign <certificate thumbprint>]
//! ```
//!
//! `build` builds the standalone application in release, the CLAP and VST3
//! bundles with Manguebyte's own `cargo mangue bundle`, the payload that
//! carries them (with the demonstration organ, the icon, the licence and the
//! notices), and the installer with the payload inside, as
//! `<target>/installer/Caecilia-Setup-<version>-x64.exe`. With `--sign` it
//! signs the application and the plugin before they are packed and the
//! installer after, with `signtool` from the Windows SDK.
//!
//! ```text
//! caecilia-dist pack --out caecilia.payload --product Caecilia --version 2.0.0
//!     --publisher "Alesson Queiroz"
//!     --file <component> <root> <path> <source>    one file
//!     --dir  <component> <root> <path> <source>    a folder, everything in it
//! ```
//!
//! `pack` builds a payload alone. `component` is `core` (always installed),
//! `app`, `vst3`, `clap` or `organ`; `root` is `program`, `vst3`, `clap` or
//! `organs`; `path` is where it goes under that root, with `/` between
//! parts. See `docs/INSTALLER.md`.

use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};

use caecilia_installer::payload::{Digest, Payload, PayloadBuilder, Root};
use caecilia_installer::text::size;

fn main() -> ExitCode {
    let mut args = std::env::args().skip(1);
    let result = match args.next().as_deref() {
        Some("build") => build(args.collect()),
        Some("pack") => pack(args.collect()).map(|_| ()),
        _ => Err(
            "caecilia-dist build [options] | caecilia-dist pack [options]; see its documentation"
                .into(),
        ),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("caecilia-dist: {e}");
            ExitCode::FAILURE
        }
    }
}

/// The repository: two folders up from this crate.
fn repository() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

/// Runs `cargo` with `args` in the repository, failing if it fails.
fn cargo(args: &[&str], env: &[(&str, &Path)], offline: bool) -> Result<(), String> {
    println!("> cargo {}", args.join(" "));
    let mut cmd = Command::new(std::env::var_os("CARGO").unwrap_or_else(|| "cargo".into()));
    cmd.args(args).current_dir(repository());
    if offline {
        cmd.env("CARGO_NET_OFFLINE", "true");
    }
    for (k, v) in env {
        cmd.env(k, v);
    }
    let status = cmd.status().map_err(|e| format!("cargo: {e}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!(
            "cargo {} failed ({status})",
            args.first().unwrap_or(&"")
        ))
    }
}

fn sign(thumbprint: &str, files: &[&Path]) -> Result<(), String> {
    let mut cmd = Command::new("signtool");
    cmd.args([
        "sign",
        "/sha1",
        thumbprint,
        "/fd",
        "SHA256",
        "/tr",
        "http://timestamp.digicert.com",
        "/td",
        "SHA256",
    ]);
    cmd.args(files);
    let status = cmd.status().map_err(|e| format!("signtool: {e}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("signtool failed ({status})"))
    }
}

/// The workspace's version, from its `Cargo.toml`.
fn workspace_version() -> Result<String, String> {
    let toml =
        std::fs::read_to_string(repository().join("Cargo.toml")).map_err(|e| e.to_string())?;
    toml.lines()
        .find_map(|l| {
            l.strip_prefix("version = \"")?
                .strip_suffix('"')
                .map(str::to_owned)
        })
        .ok_or_else(|| "no version in the workspace's Cargo.toml".into())
}

fn build(args: Vec<String>) -> Result<(), String> {
    let mut target = std::env::var_os("CARGO_TARGET_DIR")
        .map_or_else(|| repository().join("target"), PathBuf::from);
    let mut manguebyte = PathBuf::from("C:/manguebyte");
    let (mut offline, mut thumbprint) = (false, None);
    let mut it = args.into_iter();
    while let Some(arg) = it.next() {
        let mut next = |what: &str| it.next().ok_or(format!("{arg} needs {what}"));
        match arg.as_str() {
            "--target-dir" => target = PathBuf::from(next("a folder")?),
            "--manguebyte" => manguebyte = PathBuf::from(next("a folder")?),
            "--offline" => offline = true,
            "--sign" => thumbprint = Some(next("a thumbprint")?),
            other => return Err(format!("unknown argument '{other}'")),
        }
    }
    let version = workspace_version()?;
    let env = [("CARGO_TARGET_DIR", target.as_path())];

    // 1. The application, and the bundles.
    cargo(
        &[
            "build",
            "--profile",
            "caecilia-release",
            "-p",
            "caecilia-plugin",
            "--features",
            "standalone",
            "--bin",
            "caecilia",
        ],
        &env,
        offline,
    )?;
    let cargo_mangue = manguebyte.join("crates/cargo-mangue/Cargo.toml");
    cargo(
        &[
            "run",
            "--release",
            "--manifest-path",
            &cargo_mangue.to_string_lossy(),
            "--",
            "mangue",
            "bundle",
            "-p",
            "caecilia-plugin",
            "--profile",
            "caecilia-release",
            "--format",
            "clap,vst3",
        ],
        &env,
        offline,
    )?;
    let app = target.join("caecilia-release/caecilia.exe");
    let bundled = target.join("bundled");
    let clap = bundled.join("Caecilia.clap");
    let vst3 = bundled.join("Caecilia.vst3");
    if let Some(t) = &thumbprint {
        sign(
            t,
            &[&app, &clap, &vst3.join("Contents/x86_64-win/Caecilia.vst3")],
        )?;
    }

    // 2. The payload.
    let out = target.join("installer");
    let payload = out.join("caecilia.payload");
    let repo = repository();
    let s = |p: &Path| p.to_string_lossy().into_owned();
    let file = |component: &str, root: &str, path: &str, source: PathBuf| {
        [
            "--file".into(),
            component.into(),
            root.into(),
            path.into(),
            s(&source),
        ]
    };
    let mut line: Vec<String> = vec![
        "--out".into(),
        s(&payload),
        "--product".into(),
        "Caecilia".into(),
        "--version".into(),
        version.clone(),
        "--publisher".into(),
        "Alesson Queiroz".into(),
    ];
    line.extend(file(
        "core",
        "program",
        "caecilia.ico",
        repo.join("assets/installer/caecilia.ico"),
    ));
    line.extend(file("core", "program", "LICENSE.txt", repo.join("LICENSE")));
    line.extend(file("core", "program", "NOTICE.txt", repo.join("NOTICE")));
    line.extend(file(
        "core",
        "program",
        "THIRD_PARTY_NOTICES.md",
        repo.join("THIRD_PARTY_NOTICES.md"),
    ));
    line.extend(file("app", "program", "caecilia.exe", app));
    line.extend([
        "--dir".into(),
        "vst3".into(),
        "vst3".into(),
        "Caecilia.vst3".into(),
        s(&vst3),
    ]);
    line.extend(file("clap", "clap", "Caecilia.clap", clap));
    line.extend(file(
        "organ",
        "organs",
        "caecilia-pipe-organ.organ.json",
        repo.join("examples/caecilia-pipe-organ.organ.json"),
    ));
    pack(line)?;

    // 3. The installer, the payload inside it.
    cargo(
        &[
            "build",
            "--profile",
            "caecilia-release",
            "-p",
            "caecilia-installer",
            "--bin",
            "caecilia-setup",
        ],
        &[
            ("CARGO_TARGET_DIR", target.as_path()),
            ("CAECILIA_PAYLOAD", payload.as_path()),
        ],
        offline,
    )?;
    let setup = out.join(format!("Caecilia-Setup-{version}-x64.exe"));
    std::fs::copy(target.join("caecilia-release/caecilia-setup.exe"), &setup)
        .map_err(|e| e.to_string())?;
    // The payload is inside the installer now; the disk is better without
    // a copy.
    let _ = std::fs::remove_file(&payload);
    if let Some(t) = &thumbprint {
        sign(t, &[&setup])?;
    }
    let bytes = std::fs::read(&setup).map_err(|e| e.to_string())?;
    println!("{}", setup.display());
    println!(
        "  {}, SHA-256 {}",
        size(bytes.len() as u64),
        Digest::of(&bytes).hex()
    );
    Ok(())
}

fn pack(args: Vec<String>) -> Result<PathBuf, String> {
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
    Ok(out)
}

/// Every file under `dir`, with its path from there (`/` between parts),
/// in a stable order.
fn walk(dir: &Path) -> Result<Vec<(String, PathBuf)>, String> {
    let mut out = Vec::new();
    let mut stack = vec![(String::new(), dir.to_owned())];
    while let Some((prefix, d)) = stack.pop() {
        let entries = std::fs::read_dir(&d).map_err(|e| format!("{}: {e}", d.display()))?;
        for e in entries.filter_map(Result::ok) {
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
