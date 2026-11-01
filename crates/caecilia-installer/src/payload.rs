//! The payload: every file the installer places, in one block inside its
//! executable.
//!
//! ```text
//!   "CAECPAY1"            magic
//!   u32 LE                length of the manifest
//!   manifest              JSON: product, version, every file and its hash
//!   [u8; 32]              SHA-256 of the manifest
//!   packed                the distinct files, one after another, in LZMA2
//! ```
//!
//! The manifest names, for each file, the component it belongs to, the
//! folder it goes under ([`Root`]), its path there, its size and its
//! SHA-256, and where it starts in the unpacked block. Files with the same
//! bytes are stored once: the CLAP plugin and the VST3 bundle's binary are
//! the same library. The manifest also carries the SHA-256 of the packed
//! block, so a damaged installer is caught before anything is written, and
//! each file's hash is checked again once it is unpacked.

use std::collections::HashMap;
use std::fmt;
use std::io::{Read, Write};

use serde_json::{Value, json};
use sha2::{Digest as _, Sha256};

/// The first bytes of a payload.
pub const MAGIC: &[u8; 8] = b"CAECPAY1";

/// The largest dictionary the packer uses: decoding needs this much memory.
const MAX_DICT: u32 = 64 << 20;

/// A SHA-256.
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct Digest(pub [u8; 32]);

impl Digest {
    /// The SHA-256 of `bytes`.
    pub fn of(bytes: &[u8]) -> Digest {
        Digest(Sha256::digest(bytes).into())
    }

    /// The SHA-256 of everything `reader` gives.
    pub fn of_reader(mut reader: impl Read) -> std::io::Result<Digest> {
        let mut hasher = Sha256::new();
        let mut buf = vec![0; 1 << 16];
        loop {
            let n = reader.read(&mut buf)?;
            if n == 0 {
                break;
            }
            hasher.update(&buf[..n]);
        }
        Ok(Digest(hasher.finalize().into()))
    }

    /// The hash in lower-case hexadecimal.
    pub fn hex(&self) -> String {
        self.0.iter().map(|b| format!("{b:02x}")).collect()
    }

    /// Reads 64 hexadecimal digits.
    pub fn from_hex(s: &str) -> Option<Digest> {
        if s.len() != 64 {
            return None;
        }
        let mut out = [0; 32];
        for (i, byte) in out.iter_mut().enumerate() {
            *byte = u8::from_str_radix(s.get(2 * i..2 * i + 2)?, 16).ok()?;
        }
        Some(Digest(out))
    }
}

impl fmt::Debug for Digest {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.hex())
    }
}

/// The folder a file goes under; where each is depends on the scope.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Root {
    /// The program folder.
    Program,
    /// The folder hosts scan for VST3 plugins.
    Vst3,
    /// The folder hosts scan for CLAP plugins.
    Clap,
    /// The organs folder of the person installing.
    Organs,
}

impl Root {
    /// Its name in a manifest.
    pub fn name(self) -> &'static str {
        match self {
            Root::Program => "program",
            Root::Vst3 => "vst3",
            Root::Clap => "clap",
            Root::Organs => "organs",
        }
    }

    /// The root a manifest names.
    pub fn from_name(name: &str) -> Option<Root> {
        Some(match name {
            "program" => Root::Program,
            "vst3" => Root::Vst3,
            "clap" => Root::Clap,
            "organs" => Root::Organs,
            _ => return None,
        })
    }
}

/// One file of the payload.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Entry {
    /// The component it belongs to: `core` (always placed) or one the
    /// person may leave out.
    pub component: String,
    /// The folder it goes under.
    pub root: Root,
    /// Its path under that folder, with `/` between the parts.
    pub path: String,
    /// Its length in bytes.
    pub size: u64,
    /// Its SHA-256.
    pub sha256: Digest,
    /// Where its bytes start in the unpacked block.
    pub offset: u64,
}

/// What the payload says of itself.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Manifest {
    /// The product's name.
    pub product: String,
    /// The version the payload installs.
    pub version: String,
    /// Who publishes it.
    pub publisher: String,
    /// The files.
    pub entries: Vec<Entry>,
    /// The LZMA2 dictionary the block was packed with.
    pub dict_size: u32,
    /// The packed block's length.
    pub packed_size: u64,
    /// The packed block's SHA-256.
    pub packed_sha256: Digest,
    /// The unpacked block's length.
    pub unpacked_size: u64,
}

/// What is wrong with a payload.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PayloadError {
    /// There is none: a development build of the installer.
    Missing,
    /// It does not start as a payload does, or ends too soon.
    Malformed(&'static str),
    /// A hash does not match: the installer is damaged.
    Damaged(String),
}

impl fmt::Display for PayloadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PayloadError::Missing => f.write_str("this installer carries no files"),
            PayloadError::Malformed(what) => {
                write!(f, "the installer's files are malformed: {what}")
            }
            PayloadError::Damaged(what) => write!(f, "the installer is damaged: {what}"),
        }
    }
}

impl std::error::Error for PayloadError {}

/// A payload, checked: its manifest and its packed block.
#[derive(Debug)]
pub struct Payload<'a> {
    /// What it holds.
    pub manifest: Manifest,
    packed: &'a [u8],
}

impl<'a> Payload<'a> {
    /// Reads a payload and checks the hashes of its manifest and of its
    /// packed block.
    pub fn parse(bytes: &'a [u8]) -> Result<Payload<'a>, PayloadError> {
        if bytes.is_empty() {
            return Err(PayloadError::Missing);
        }
        let rest = bytes
            .strip_prefix(MAGIC.as_slice())
            .ok_or(PayloadError::Malformed("no magic"))?;
        let (len, rest) = rest
            .split_first_chunk::<4>()
            .ok_or(PayloadError::Malformed("no manifest length"))?;
        let len = u32::from_le_bytes(*len) as usize;
        if rest.len() < len + 32 {
            return Err(PayloadError::Malformed("manifest cut short"));
        }
        let (text, rest) = rest.split_at(len);
        let (hash, packed) = rest.split_at(32);
        if Digest::of(text).0 != hash {
            return Err(PayloadError::Damaged(
                "the manifest's hash does not match".into(),
            ));
        }
        let manifest = manifest_from_json(text)?;
        if packed.len() as u64 != manifest.packed_size {
            return Err(PayloadError::Damaged(format!(
                "{} bytes of files where there should be {}",
                packed.len(),
                manifest.packed_size
            )));
        }
        if Digest::of(packed) != manifest.packed_sha256 {
            return Err(PayloadError::Damaged(
                "the files' hash does not match".into(),
            ));
        }
        Ok(Payload { manifest, packed })
    }

    /// Unpacks the files and checks each one's hash.
    pub fn unpack(&self) -> Result<Unpacked, PayloadError> {
        let mut data = Vec::with_capacity(self.manifest.unpacked_size as usize);
        lzma_rust2::Lzma2Reader::new(self.packed, self.manifest.dict_size, None)
            .read_to_end(&mut data)
            .map_err(|e| PayloadError::Damaged(format!("the files do not unpack: {e}")))?;
        if data.len() as u64 != self.manifest.unpacked_size {
            return Err(PayloadError::Damaged(
                "the files unpack to the wrong length".into(),
            ));
        }
        let mut checked = HashMap::new();
        for entry in &self.manifest.entries {
            let range = range_of(entry, data.len())?;
            let ok = *checked
                .entry(entry.offset)
                .or_insert_with(|| Digest::of(&data[range]) == entry.sha256);
            if !ok {
                return Err(PayloadError::Damaged(format!(
                    "{} does not match its hash",
                    entry.path
                )));
            }
        }
        Ok(Unpacked {
            manifest: self.manifest.clone(),
            data,
        })
    }
}

fn range_of(entry: &Entry, len: usize) -> Result<std::ops::Range<usize>, PayloadError> {
    let start = entry.offset as usize;
    let end = start
        .checked_add(entry.size as usize)
        .filter(|&end| end <= len)
        .ok_or(PayloadError::Malformed("a file reaches past the block"))?;
    Ok(start..end)
}

/// The files of a payload, unpacked and checked.
#[derive(Debug, Clone)]
pub struct Unpacked {
    /// What they are.
    pub manifest: Manifest,
    data: Vec<u8>,
}

impl Unpacked {
    /// The bytes of `entry`, one of [`Manifest::entries`].
    pub fn bytes(&self, entry: &Entry) -> &[u8] {
        &self.data[entry.offset as usize..(entry.offset + entry.size) as usize]
    }
}

fn manifest_from_json(text: &[u8]) -> Result<Manifest, PayloadError> {
    let bad = PayloadError::Malformed("the manifest is not what it should be");
    let v: Value = serde_json::from_slice(text).map_err(|_| bad.clone())?;
    let s = |v: &Value, k: &str| v.get(k).and_then(Value::as_str).map(str::to_owned);
    let n = |v: &Value, k: &str| v.get(k).and_then(Value::as_u64);
    let digest = |v: &Value, k: &str| v.get(k).and_then(Value::as_str).and_then(Digest::from_hex);
    if n(&v, "format") != Some(1) {
        return Err(PayloadError::Malformed("an unknown manifest format"));
    }
    let mut entries = Vec::new();
    for e in v
        .get("files")
        .and_then(Value::as_array)
        .ok_or(bad.clone())?
    {
        let path = s(e, "path").ok_or(bad.clone())?;
        // A path only ever goes down from its root.
        if path.is_empty()
            || path.contains('\\')
            || path.contains(':')
            || path
                .split('/')
                .any(|p| p.is_empty() || p == "." || p == "..")
        {
            return Err(PayloadError::Malformed("a file's path leaves its folder"));
        }
        entries.push(Entry {
            component: s(e, "component").ok_or(bad.clone())?,
            root: s(e, "root")
                .as_deref()
                .and_then(Root::from_name)
                .ok_or(bad.clone())?,
            path,
            size: n(e, "size").ok_or(bad.clone())?,
            sha256: digest(e, "sha256").ok_or(bad.clone())?,
            offset: n(e, "offset").ok_or(bad.clone())?,
        });
    }
    Ok(Manifest {
        product: s(&v, "product").ok_or(bad.clone())?,
        version: s(&v, "version").ok_or(bad.clone())?,
        publisher: s(&v, "publisher").ok_or(bad.clone())?,
        entries,
        dict_size: n(&v, "dict_size")
            .and_then(|d| u32::try_from(d).ok())
            .ok_or(bad.clone())?,
        packed_size: n(&v, "packed_size").ok_or(bad.clone())?,
        packed_sha256: digest(&v, "packed_sha256").ok_or(bad.clone())?,
        unpacked_size: n(&v, "unpacked_size").ok_or(bad)?,
    })
}

/// Builds a payload, file by file.
#[derive(Debug, Default)]
pub struct PayloadBuilder {
    product: String,
    version: String,
    publisher: String,
    entries: Vec<Entry>,
    data: Vec<u8>,
    stored: HashMap<Digest, u64>,
}

impl PayloadBuilder {
    /// A payload for `version` of `product`, by `publisher`.
    pub fn new(product: &str, version: &str, publisher: &str) -> PayloadBuilder {
        PayloadBuilder {
            product: product.into(),
            version: version.into(),
            publisher: publisher.into(),
            ..PayloadBuilder::default()
        }
    }

    /// Adds `bytes` as `path` under `root`, in `component`. Bytes already
    /// added under another path are stored once.
    pub fn add(&mut self, component: &str, root: Root, path: &str, bytes: &[u8]) {
        let sha256 = Digest::of(bytes);
        let offset = *self.stored.entry(sha256).or_insert_with(|| {
            let at = self.data.len() as u64;
            self.data.extend_from_slice(bytes);
            at
        });
        self.entries.push(Entry {
            component: component.into(),
            root,
            path: path.into(),
            size: bytes.len() as u64,
            sha256,
            offset,
        });
    }

    /// The bytes of the distinct files, before packing.
    pub fn unpacked_size(&self) -> u64 {
        self.data.len() as u64
    }

    /// The payload.
    pub fn build(self) -> std::io::Result<Vec<u8>> {
        // The smallest dictionary that holds everything: decoding needs no
        // more memory than that.
        let dict_size = (self.data.len() as u32)
            .clamp(1 << 16, MAX_DICT)
            .next_power_of_two()
            .min(MAX_DICT);
        let mut options = lzma_rust2::Lzma2Options::with_preset(9);
        options.lzma_options.dict_size = dict_size;
        let mut writer = lzma_rust2::Lzma2Writer::new(Vec::new(), options);
        writer.write_all(&self.data)?;
        let packed = writer.finish().map_err(std::io::Error::other)?;
        let files: Vec<Value> = self
            .entries
            .iter()
            .map(|e| {
                json!({
                    "component": e.component,
                    "root": e.root.name(),
                    "path": e.path,
                    "size": e.size,
                    "sha256": e.sha256.hex(),
                    "offset": e.offset,
                })
            })
            .collect();
        let manifest = json!({
            "format": 1,
            "product": self.product,
            "version": self.version,
            "publisher": self.publisher,
            "dict_size": dict_size,
            "packed_size": packed.len(),
            "packed_sha256": Digest::of(&packed).hex(),
            "unpacked_size": self.data.len(),
            "files": files,
        });
        let text = serde_json::to_vec_pretty(&manifest).map_err(std::io::Error::other)?;
        let mut out = Vec::with_capacity(packed.len() + text.len() + 44);
        out.extend_from_slice(MAGIC);
        out.extend_from_slice(&(text.len() as u32).to_le_bytes());
        out.extend_from_slice(&text);
        out.extend_from_slice(&Digest::of(&text).0);
        out.extend_from_slice(&packed);
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Vec<u8> {
        let plugin: Vec<u8> = (0..50_000u32).map(|i| (i * 7 % 251) as u8).collect();
        let mut b = PayloadBuilder::new("Caecilia", "2.0.0", "Alesson Queiroz");
        b.add("app", Root::Program, "caecilia.exe", b"MZ the app");
        b.add(
            "vst3",
            Root::Vst3,
            "Caecilia.vst3/Contents/x86_64-win/Caecilia.vst3",
            &plugin,
        );
        b.add("clap", Root::Clap, "Caecilia.clap", &plugin);
        b.add("core", Root::Program, "LICENSE.txt", b"Apache-2.0");
        b.add("organ", Root::Organs, "caecilia-demo.organ.json", b"{}");
        b.add("core", Root::Program, "empty.txt", b"");
        b.build().unwrap()
    }

    #[test]
    fn a_payload_round_trips_byte_for_byte() {
        let bytes = sample();
        let payload = Payload::parse(&bytes).unwrap();
        let m = &payload.manifest;
        assert_eq!(
            (m.product.as_str(), m.version.as_str()),
            ("Caecilia", "2.0.0")
        );
        let files = payload.unpack().unwrap();
        let get = |path: &str| {
            let e = m.entries.iter().find(|e| e.path == path).unwrap();
            files.bytes(e).to_vec()
        };
        assert_eq!(get("caecilia.exe"), b"MZ the app");
        assert_eq!(get("LICENSE.txt"), b"Apache-2.0");
        assert_eq!(get("empty.txt"), b"");
        assert_eq!(get("Caecilia.clap").len(), 50_000);
        assert_eq!(
            get("Caecilia.clap"),
            get("Caecilia.vst3/Contents/x86_64-win/Caecilia.vst3")
        );
    }

    #[test]
    fn identical_files_are_stored_once() {
        let bytes = sample();
        let m = Payload::parse(&bytes).unwrap().manifest;
        let clap = m.entries.iter().find(|e| e.root == Root::Clap).unwrap();
        let vst3 = m.entries.iter().find(|e| e.root == Root::Vst3).unwrap();
        assert_eq!(clap.offset, vst3.offset);
        assert_eq!(m.unpacked_size, 10 + 50_000 + 10 + 2);
    }

    #[test]
    fn a_flipped_byte_anywhere_is_caught() {
        let bytes = sample();
        // Every 97th byte, and the last: the magic, the manifest, its hash,
        // the packed files.
        for at in (0..bytes.len()).step_by(97).chain([bytes.len() - 1]) {
            let mut bad = bytes.clone();
            bad[at] ^= 0x20;
            let caught = match Payload::parse(&bad) {
                Err(_) => true,
                Ok(p) => p.unpack().is_err(),
            };
            assert!(caught, "a change at byte {at} went unnoticed");
        }
    }

    #[test]
    fn no_payload_and_a_cut_one_say_so() {
        assert_eq!(Payload::parse(&[]).unwrap_err(), PayloadError::Missing);
        let bytes = sample();
        assert!(Payload::parse(&bytes[..bytes.len() - 1]).is_err());
        assert!(Payload::parse(&bytes[..20]).is_err());
    }

    #[test]
    fn a_path_that_climbs_out_of_its_folder_is_refused() {
        for path in [
            "../evil.exe",
            "a/../../b",
            "C:/Windows/x",
            "a\\b",
            "/abs",
            "a//b",
        ] {
            let mut b = PayloadBuilder::new("Caecilia", "2.0.0", "x");
            b.add("core", Root::Program, path, b"x");
            let bytes = b.build().unwrap();
            assert!(
                matches!(Payload::parse(&bytes), Err(PayloadError::Malformed(_))),
                "{path} was taken"
            );
        }
    }

    #[test]
    fn digests_read_back_from_hex() {
        let d = Digest::of(b"abc");
        assert_eq!(
            d.hex(),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        assert_eq!(Digest::from_hex(&d.hex()), Some(d));
        assert_eq!(Digest::from_hex("zz"), None);
    }
}
