//! What `tools/reference` measures, measured the same way.

#![allow(dead_code)]

use caecilia_engine::{Command, OrganEngine, Timed};
use serde_json::Value;

pub const SR: f64 = 48000.0;
pub const BLOCK: usize = 256;

pub fn reference() -> Value {
    let path = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../reference/cpp-1.x/reference.json"
    );
    serde_json::from_str(&std::fs::read_to_string(path).expect("reference.json")).expect("JSON")
}

pub fn f(v: &Value) -> f64 {
    v.as_f64().expect("a number")
}

pub fn floats(v: &Value) -> Vec<f64> {
    v.as_array().expect("an array").iter().map(f).collect()
}

pub fn blocks_for(seconds: f64) -> usize {
    (seconds * SR / BLOCK as f64).ceil() as usize
}

pub struct Render {
    pub l: Vec<f32>,
    pub r: Vec<f32>,
}

/// Render `blocks` blocks; `before(b)` returns the commands for block `b` (at
/// frame 0) and `after(b, engine)` observes the engine once it has run.
pub fn run(
    engine: &mut OrganEngine,
    blocks: usize,
    mut before: impl FnMut(usize) -> Vec<Command>,
    mut after: impl FnMut(usize, &OrganEngine),
) -> Render {
    let mut x = Render {
        l: vec![0.0; blocks * BLOCK],
        r: vec![0.0; blocks * BLOCK],
    };
    for b in 0..blocks {
        let events: Vec<Timed> = before(b)
            .into_iter()
            .map(|command| Timed { offset: 0, command })
            .collect();
        let span = b * BLOCK..(b + 1) * BLOCK;
        engine.process(&mut x.l[span.clone()], &mut x.r[span], &events);
        after(b, engine);
    }
    x
}

pub fn db(v: f64) -> f64 {
    20.0 * v.max(1e-12).log10()
}

pub fn rms(x: &Render, from: usize, to: usize) -> f64 {
    let s: f64 = (from..to)
        .map(|i| f64::from(x.l[i]).powi(2) + f64::from(x.r[i]).powi(2))
        .sum();
    if to > from {
        (s / (2.0 * (to - from) as f64)).sqrt()
    } else {
        0.0
    }
}

pub fn envelope(x: &Render, win: usize) -> Vec<f64> {
    (0..x.l.len() / win)
        .map(|w| rms(x, w * win, (w + 1) * win))
        .collect()
}

/// Hann-windowed Goertzel of (l+r)/2, normalised so a unit sine reads 1.
pub fn goertzel(x: &Render, hz: f64, from: usize, to: usize) -> f64 {
    let w = std::f64::consts::TAU * hz / SR;
    let c = 2.0 * w.cos();
    let (mut s1, mut s2) = (0.0f64, 0.0f64);
    let n = to - from;
    for i in from..to {
        let hann = 0.5 - 0.5 * (std::f64::consts::TAU * (i - from) as f64 / (n - 1) as f64).cos();
        let s0 = hann * 0.5 * (f64::from(x.l[i]) + f64::from(x.r[i])) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    let re = s1 - s2 * w.cos();
    let im = s2 * w.sin();
    2.0 * (re * re + im * im).sqrt() / (0.5 * n as f64)
}

pub fn correlation(x: &Render, from: usize, to: usize) -> f64 {
    let (mut lr, mut ll, mut rr) = (0.0, 0.0, 0.0);
    for i in from..to {
        let (a, b) = (f64::from(x.l[i]), f64::from(x.r[i]));
        lr += a * b;
        ll += a * a;
        rr += b * b;
    }
    if ll > 0.0 && rr > 0.0 {
        lr / (ll * rr).sqrt()
    } else {
        1.0
    }
}
