// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Gives the standalone `caecilia.exe` the Caecilia's icon, which Explorer,
//! the taskbar and the shortcuts show. The icon is written straight into a
//! `.res` file the linker takes, so no resource compiler is needed; the
//! plugin itself gets nothing.

use std::path::{Path, PathBuf};

const RT_ICON: u16 = 3;
const RT_GROUP_ICON: u16 = 14;
/// English (United States), as the installer's resources.
const LANG: u16 = 0x0409;

fn main() {
    let windows = std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows");
    let msvc = std::env::var("CARGO_CFG_TARGET_ENV").as_deref() == Ok("msvc");
    let standalone = std::env::var_os("CARGO_FEATURE_STANDALONE").is_some();
    if windows && msvc && standalone {
        let out = PathBuf::from(std::env::var_os("OUT_DIR").expect("cargo sets OUT_DIR"));
        let icon = Path::new("../../assets/installer/caecilia.ico");
        println!("cargo:rerun-if-changed={}", icon.display());
        let res = out.join("caecilia.res");
        std::fs::write(&res, resources(icon)).expect("write the resources");
        println!("cargo:rustc-link-arg-bin=caecilia={}", res.display());
    }
}

/// The icon as resources: each image, and the group that names them.
fn resources(icon: &Path) -> Vec<u8> {
    let mut res = Vec::new();
    // A .res file opens with an empty entry.
    entry(&mut res, 0, 0, &[]);
    let ico = std::fs::read(icon).unwrap_or_else(|e| panic!("{}: {e}", icon.display()));
    let u16_at = |at: usize| u16::from_le_bytes([ico[at], ico[at + 1]]);
    let u32_at = |at: usize| u32::from_le_bytes(ico[at..at + 4].try_into().expect("four bytes"));
    let count = u16_at(4);
    let mut group = Vec::new();
    group.extend_from_slice(&[0, 0, 1, 0]);
    group.extend_from_slice(&count.to_le_bytes());
    for i in 0..count {
        let e = 6 + 16 * i as usize;
        let (size, offset) = (u32_at(e + 8) as usize, u32_at(e + 12) as usize);
        let id = i + 1;
        entry(&mut res, RT_ICON, id, &ico[offset..offset + size]);
        // The directory entry, with the resource's id where the file has
        // the image's offset.
        group.extend_from_slice(&ico[e..e + 12]);
        group.extend_from_slice(&id.to_le_bytes());
    }
    entry(&mut res, RT_GROUP_ICON, 1, &group);
    res
}

/// One resource of numbered `kind` and `id`.
fn entry(res: &mut Vec<u8>, kind: u16, id: u16, data: &[u8]) {
    res.extend_from_slice(&(data.len() as u32).to_le_bytes());
    res.extend_from_slice(&32u32.to_le_bytes());
    for v in [0xFFFF, kind, 0xFFFF, id] {
        res.extend_from_slice(&u16::to_le_bytes(v));
    }
    res.extend_from_slice(&0u32.to_le_bytes()); // data version
    // Moveable, pure; discardable for all but the empty entry.
    let flags: u16 = if kind == 0 { 0 } else { 0x1030 };
    res.extend_from_slice(&flags.to_le_bytes());
    res.extend_from_slice(&(if kind == 0 { 0 } else { LANG }).to_le_bytes());
    res.extend_from_slice(&0u32.to_le_bytes()); // version
    res.extend_from_slice(&0u32.to_le_bytes()); // characteristics
    res.extend_from_slice(data);
    while res.len() % 4 != 0 {
        res.push(0);
    }
}
