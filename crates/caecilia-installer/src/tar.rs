//! An archive of one file. A file downloaded from a release page loses the
//! mark that lets it run on Linux and macOS, and a tar archive keeps it.
//! The payload inside the installer is packed already, so the archive is
//! not compressed.

const BLOCK: usize = 512;

/// Writes `value` in octal into `field`, ending in NUL, as tar headers do.
fn octal(field: &mut [u8], value: u64) {
    let digits = field.len() - 1;
    let text = format!("{value:0digits$o}");
    field[..digits].copy_from_slice(&text.as_bytes()[text.len() - digits..]);
    field[digits] = 0;
}

/// A POSIX tar archive holding only `bytes`, named `name` (at most 100
/// bytes, or `None`), with the Unix permissions `mode` and the time `mtime`
/// in seconds since 1970.
pub fn single_file(name: &str, mode: u32, mtime: u64, bytes: &[u8]) -> Option<Vec<u8>> {
    if name.is_empty() || name.len() > 100 {
        return None;
    }
    let mut header = [0u8; BLOCK];
    header[..name.len()].copy_from_slice(name.as_bytes());
    octal(&mut header[100..108], u64::from(mode));
    octal(&mut header[108..116], 0);
    octal(&mut header[116..124], 0);
    octal(&mut header[124..136], bytes.len() as u64);
    octal(&mut header[136..148], mtime);
    header[156] = b'0';
    header[257..263].copy_from_slice(b"ustar\0");
    header[263..265].copy_from_slice(b"00");
    // The sum of every byte, its own field counted as spaces.
    header[148..156].fill(b' ');
    let sum: u32 = header.iter().map(|&b| u32::from(b)).sum();
    octal(&mut header[148..155], u64::from(sum));
    header[155] = b' ';

    let padding = (BLOCK - bytes.len() % BLOCK) % BLOCK;
    let mut out = Vec::with_capacity(BLOCK + bytes.len() + padding + 2 * BLOCK);
    out.extend_from_slice(&header);
    out.extend_from_slice(bytes);
    out.resize(out.len() + padding + 2 * BLOCK, 0);
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn field(header: &[u8], range: std::ops::Range<usize>) -> u64 {
        let text = std::str::from_utf8(&header[range]).unwrap();
        u64::from_str_radix(text.trim_matches(['\0', ' ']), 8).unwrap()
    }

    #[test]
    fn the_archive_reads_back() {
        let bytes = vec![7u8; 1000];
        let tar = single_file("Caecilia-Setup", 0o755, 1_793_570_400, &bytes).unwrap();
        assert_eq!(tar.len(), 512 + 1024 + 1024, "blocks of 512, two to end");
        assert_eq!(tar.len() % 512, 0);
        let header = &tar[..512];
        assert_eq!(&header[..14], b"Caecilia-Setup");
        assert_eq!(header[14], 0);
        assert_eq!(field(header, 100..108), 0o755);
        assert_eq!(field(header, 124..136), 1000);
        assert_eq!(field(header, 136..148), 1_793_570_400);
        assert_eq!(&header[257..263], b"ustar\0");
        assert_eq!(&tar[512..1512], &bytes[..]);
        assert!(tar[1512..].iter().all(|&b| b == 0));
        let mut blanked = header.to_vec();
        blanked[148..156].fill(b' ');
        let sum: u64 = blanked.iter().map(|&b| u64::from(b)).sum();
        assert_eq!(field(header, 148..156), sum);
    }

    #[test]
    fn a_whole_number_of_blocks_needs_no_padding() {
        let tar = single_file("a", 0o644, 0, &[1; 512]).unwrap();
        assert_eq!(tar.len(), 512 + 512 + 1024);
    }

    #[test]
    fn a_name_too_long_is_refused() {
        assert!(single_file(&"n".repeat(101), 0o644, 0, b"").is_none());
        assert!(single_file("", 0o644, 0, b"").is_none());
    }
}
