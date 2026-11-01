// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The standalone application never holds the plugin it plays: the instance
//! leaves its console for it, once.

use caecilia_plugin::Caecilia;
use manguebyte_plugin::Harness;

#[test]
fn an_instance_leaves_its_console_for_the_application_once() {
    assert!(caecilia_plugin::console_of_last_instance().is_none());
    caecilia_plugin::keep_console_of_new_instances();
    let h = Harness::<Caecilia>::new(48000.0, 256);
    let (_, params) = caecilia_plugin::console_of_last_instance().expect("kept");
    assert!(std::ptr::eq(&*params, h.params()));
    assert!(caecilia_plugin::console_of_last_instance().is_none());
}
