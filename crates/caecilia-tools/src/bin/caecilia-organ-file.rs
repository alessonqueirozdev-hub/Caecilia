// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Organ files from the command line.
//!
//!   caecilia-organ-file --export            the demonstration organ, as a file to start from
//!   caecilia-organ-file --check <file>...   every error and warning, as the plugin would see them
//!
//! `--check` exits 1 if any file does not load.

use caecilia_model::{OrganFileError, demo_organ, read_organ_file, write_organ};

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args.first().map(String::as_str) {
        Some("--export") => print!("{}", write_organ(&demo_organ())),
        Some("--check") if args.len() > 1 => {
            let mut failed = false;
            for path in &args[1..] {
                match read_organ_file(std::path::Path::new(path)) {
                    Ok(doc) => {
                        let o = &doc.organ;
                        println!(
                            "{path}: ok -- {} ({} stops, {} divisions, {} windchests, {} couplers)",
                            o.name,
                            o.stops().len(),
                            o.divisions().len(),
                            o.windchests().len(),
                            o.couplers().len()
                        );
                        for w in &doc.warnings {
                            println!("{path}: warning: {w}");
                        }
                    }
                    Err(OrganFileError::Invalid { errors, warnings }) => {
                        failed = true;
                        for e in &errors {
                            println!("{path}: error: {e}");
                        }
                        for w in &warnings {
                            println!("{path}: warning: {w}");
                        }
                    }
                    Err(e) => {
                        failed = true;
                        println!("{path}: error: {e}");
                    }
                }
            }
            std::process::exit(i32::from(failed));
        }
        _ => {
            eprintln!("caecilia-organ-file --export | --check <file>...");
            std::process::exit(2);
        }
    }
}
