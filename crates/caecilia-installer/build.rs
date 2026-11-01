//! Embeds the payload `CAECILIA_PAYLOAD` names (none for a development
//! build), and gives `caecilia-setup` its Windows resources: the manifest
//! (no administrator to open it, per-monitor DPI, long paths), the icon
//! and the version. The resources are written straight into a `.res` file
//! the linker takes, so no resource compiler is needed.

use std::path::{Path, PathBuf};

fn main() {
    let out = PathBuf::from(std::env::var_os("OUT_DIR").expect("cargo sets OUT_DIR"));
    embed_payload(&out);
    let windows = std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows");
    let msvc = std::env::var("CARGO_CFG_TARGET_ENV").as_deref() == Ok("msvc");
    if windows && msvc {
        let icon = Path::new("../../assets/installer/caecilia.ico");
        println!("cargo:rerun-if-changed={}", icon.display());
        let res = out.join("caecilia-setup.res");
        std::fs::write(&res, resources(icon)).expect("write the resources");
        println!("cargo:rustc-link-arg-bin=caecilia-setup={}", res.display());
    }
}

fn embed_payload(out: &Path) {
    println!("cargo:rerun-if-env-changed=CAECILIA_PAYLOAD");
    let payload = out.join("payload.bin");
    match std::env::var_os("CAECILIA_PAYLOAD") {
        Some(path) => {
            let path = PathBuf::from(path);
            println!("cargo:rerun-if-changed={}", path.display());
            std::fs::copy(&path, &payload)
                .unwrap_or_else(|e| panic!("CAECILIA_PAYLOAD {}: {e}", path.display()));
        }
        None => std::fs::write(&payload, []).expect("write an empty payload"),
    }
}

const RT_ICON: u16 = 3;
const RT_GROUP_ICON: u16 = 14;
const RT_VERSION: u16 = 16;
const RT_MANIFEST: u16 = 24;
/// English (United States): the language the resources are filed under.
const LANG: u16 = 0x0409;

fn manifest(version: &str) -> String {
    format!(
        r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="Caecilia.Setup" version="{version}" processorArchitecture="amd64"/>
  <description>Caecilia Setup</description>
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="asInvoker" uiAccess="false"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application>
      <supportedOS Id="{{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}}"/>
    </application>
  </compatibility>
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
      <longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>
    </windowsSettings>
  </application>
  <dependency>
    <dependentAssembly>
      <assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0" processorArchitecture="*" publicKeyToken="6595b64144ccf1df" language="*"/>
    </dependentAssembly>
  </dependency>
</assembly>
"#
    )
}

/// The version's four numbers: `2.0.0-alpha.0` is 2.0.0.0.
fn numbers(version: &str) -> [u16; 4] {
    let core = version.split(['-', '+']).next().unwrap_or("");
    let mut n = [0; 4];
    for (slot, part) in n.iter_mut().zip(core.split('.')) {
        *slot = part.parse().unwrap_or(0);
    }
    n
}

fn resources(icon: &Path) -> Vec<u8> {
    let version = std::env::var("CARGO_PKG_VERSION").expect("cargo sets the version");
    let n = numbers(&version);
    let dotted = format!("{}.{}.{}.{}", n[0], n[1], n[2], n[3]);
    let mut res = Vec::new();
    // A .res file opens with an empty entry.
    entry(&mut res, 0, 0, &[]);
    entry(&mut res, RT_MANIFEST, 1, manifest(&dotted).as_bytes());

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
    entry(&mut res, RT_VERSION, 1, &version_info(n, &version));
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
    pad(res);
}

fn pad(v: &mut Vec<u8>) {
    while v.len() % 4 != 0 {
        v.push(0);
    }
}

fn utf16z(s: &str) -> Vec<u8> {
    s.encode_utf16()
        .chain([0])
        .flat_map(u16::to_le_bytes)
        .collect()
}

/// A node of a version resource: its key, its value and its children,
/// each on a four-byte boundary.
fn node(key: &str, value: &[u8], value_len: u16, text: bool, children: &[Vec<u8>]) -> Vec<u8> {
    let mut v = vec![0, 0];
    v.extend_from_slice(&value_len.to_le_bytes());
    v.extend_from_slice(&u16::from(text).to_le_bytes());
    v.extend_from_slice(&utf16z(key));
    pad(&mut v);
    v.extend_from_slice(value);
    for child in children {
        pad(&mut v);
        v.extend_from_slice(child);
    }
    let len = v.len() as u16;
    v[..2].copy_from_slice(&len.to_le_bytes());
    v
}

fn version_info(n: [u16; 4], version: &str) -> Vec<u8> {
    let ms = (u32::from(n[0]) << 16) | u32::from(n[1]);
    let ls = (u32::from(n[2]) << 16) | u32::from(n[3]);
    let mut fixed = Vec::new();
    for v in [
        0xFEEF_04BD, // signature
        0x0001_0000, // structure version
        ms,
        ls, // file version
        ms,
        ls,       // product version
        0x3F,     // flags mask
        0,        // flags
        0x4_0004, // VOS_NT_WINDOWS32
        1,        // VFT_APP
        0,
        0,
        0, // subtype, date
    ] {
        fixed.extend_from_slice(&u32::to_le_bytes(v));
    }
    let strings: Vec<Vec<u8>> = [
        ("CompanyName", "Alesson Queiroz"),
        ("FileDescription", "Caecilia Setup"),
        ("FileVersion", version),
        ("InternalName", "caecilia-setup"),
        (
            "LegalCopyright",
            "© 2026 Alesson Queiroz and the Caecilia contributors. Apache-2.0.",
        ),
        ("OriginalFilename", "caecilia-setup.exe"),
        ("ProductName", "Caecilia"),
        ("ProductVersion", version),
    ]
    .iter()
    .map(|(k, v)| {
        let value = utf16z(v);
        node(k, &value, (value.len() / 2) as u16, true, &[])
    })
    .collect();
    let table = node("040904b0", &[], 0, true, &strings);
    let string_info = node("StringFileInfo", &[], 0, true, &[table]);
    let translation: Vec<u8> = [LANG, 1200].iter().flat_map(|v| v.to_le_bytes()).collect();
    let var = node("Translation", &translation, 4, false, &[]);
    let var_info = node("VarFileInfo", &[], 0, true, &[var]);
    node(
        "VS_VERSION_INFO",
        &fixed,
        52,
        false,
        &[string_info, var_info],
    )
}
