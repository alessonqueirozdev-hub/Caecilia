//! Embeds the payload `CAECILIA_PAYLOAD` names (none for a development
//! build).

use std::path::PathBuf;

fn main() {
    let out = PathBuf::from(std::env::var_os("OUT_DIR").expect("cargo sets OUT_DIR"));
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
