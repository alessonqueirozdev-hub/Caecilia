// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The small numerical pieces the partial bank is built from.

use std::sync::LazyLock;

/// `1/log10(2)`, for dB to `exp2`.
const INV_LOG10_OF_2: f32 = std::f32::consts::LOG2_10;
/// `ln(2)/1200`: cents to ratio, linearised.
#[allow(clippy::excessive_precision)]
const LN2_OVER_1200: f32 = 0.000_577_622_65;

/// `10^(db/20)`, through `exp2`.
#[inline]
pub(crate) fn db_to_linear(db: f32) -> f32 {
    (db * (INV_LOG10_OF_2 * 0.05)).exp2()
}

/// `2^(c/1200) ~ 1 + c ln2/1200`: within 1e-4 for the few tens of cents that
/// drift and glide reach, without a `powf` per partial.
#[inline]
pub(crate) fn cents_to_ratio_approx(cents: f32) -> f32 {
    1.0 + cents * LN2_OVER_1200
}

/// Smoothstep easing on [0, 1].
#[inline]
pub(crate) fn smoothstep01(x: f32) -> f32 {
    if x <= 0.0 {
        0.0
    } else if x >= 1.0 {
        1.0
    } else {
        x * x * (3.0 - 2.0 * x)
    }
}

/// One xorshift step, mapped to [-1, 1).
#[inline]
pub(crate) fn next_bipolar(s: &mut u32) -> f32 {
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    (*s as i32) as f32 * (1.0 / 2_147_483_648.0)
}

/// A 32-bit finaliser.
#[inline]
pub(crate) fn hash32(mut x: u32) -> u32 {
    x ^= x >> 16;
    x = x.wrapping_mul(0x7FEB_352D);
    x ^= x >> 15;
    x = x.wrapping_mul(0x846C_A68B);
    x ^= x >> 16;
    x
}

/// `sin(2 pi turns)` by an odd minimax polynomial (error 5.9e-7).
#[inline]
pub(crate) fn fast_sine_turns(turns: f32) -> f32 {
    let t = turns - (turns + 0.5).floor();
    let a = t.abs();
    let folded = 0.25 - (a - 0.25).abs();
    let x = std::f32::consts::TAU * folded.copysign(t);
    let x2 = x * x;
    let mut s = -0.000_183_637_f32;
    s = s * x2 + 0.008_306_325;
    s = s * x2 - 0.166_648_28;
    s = s * x2 + 0.999_996_6;
    s * x
}

/// A point on the unit circle.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub(crate) struct Phasor {
    pub cos: f32,
    pub sin: f32,
}

const UNIT_CIRCLE: usize = 256;
const PAN_STEPS: usize = 256;

pub(crate) struct Tables {
    unit_circle: [Phasor; UNIT_CIRCLE],
    pan: [Phasor; PAN_STEPS + 1],
}

/// Built once, off the audio thread: [`crate::PartialBank::new`] touches it.
pub(crate) static TABLES: LazyLock<Tables> = LazyLock::new(|| {
    let mut unit_circle = [Phasor::default(); UNIT_CIRCLE];
    for (i, p) in unit_circle.iter_mut().enumerate() {
        let a = std::f64::consts::TAU * i as f64 / UNIT_CIRCLE as f64;
        *p = Phasor {
            cos: a.cos() as f32,
            sin: a.sin() as f32,
        };
    }
    let mut pan = [Phasor::default(); PAN_STEPS + 1];
    for (i, p) in pan.iter_mut().enumerate() {
        let x = -1.0 + 2.0 * i as f64 / PAN_STEPS as f64;
        let theta = (x + 1.0) * 0.25 * std::f64::consts::PI;
        *p = Phasor {
            cos: theta.cos() as f32,
            sin: theta.sin() as f32,
        };
    }
    Tables { unit_circle, pan }
});

/// An arbitrary point on the unit circle from well-mixed bits: a start phase
/// only has to be scattered, and a table read is a fifteenth of a cos/sin pair.
#[inline]
pub(crate) fn random_phasor(bits: u32) -> Phasor {
    TABLES.unit_circle[(bits as usize) & (UNIT_CIRCLE - 1)]
}

/// Equal-power pan gains (left, right) for `pan` in [-1, 1], from a 257-entry
/// table with linear interpolation.
#[inline]
pub(crate) fn equal_power_pan(pan: f32) -> (f32, f32) {
    let u = (pan.clamp(-1.0, 1.0) + 1.0) * (0.5 * PAN_STEPS as f32);
    let i = (u as usize).min(PAN_STEPS - 1);
    let f = u - i as f32;
    let (a, b) = (TABLES.pan[i], TABLES.pan[i + 1]);
    (a.cos + (b.cos - a.cos) * f, a.sin + (b.sin - a.sin) * f)
}

/// The scalar partial kernel 1.x rendered with, kept as the reference the
/// framework's SIMD bank is tested against: one partial's run of frames,
/// accumulated into L and R.
///
/// A quadrature phasor rotated per sample, under a per-sample gain ramp. The
/// scalar reference; the framework's SIMD bank replaces it behind this shape.
#[cfg(test)]
#[allow(clippy::too_many_arguments)]
pub(crate) fn partial_accumulate(
    left: &mut [f32],
    right: &mut [f32],
    osc: &mut Phasor,
    cos_inc: f32,
    sin_inc: f32,
    gain: &mut f32,
    gain_inc: f32,
    pan_l: f32,
    pan_r: f32,
) {
    let (mut x, mut y, mut g) = (osc.cos, osc.sin, *gain);
    for (l, r) in left.iter_mut().zip(right.iter_mut()) {
        g += gain_inc;
        let v = g * y;
        *l += v * pan_l;
        *r += v * pan_r;
        let nx = x * cos_inc - y * sin_inc;
        let ny = x * sin_inc + y * cos_inc;
        x = nx;
        y = ny;
    }
    *osc = Phasor { cos: x, sin: y };
    *gain = g;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_fast_sine_is_a_sine() {
        for i in 0..1000 {
            let t = i as f32 / 997.0 - 0.3;
            let err = (fast_sine_turns(t) - (std::f32::consts::TAU * t).sin()).abs();
            assert!(err < 2e-6, "{t}: {err}");
        }
    }

    #[test]
    fn the_framework_bank_renders_a_partial_exactly_as_the_reference_kernel() {
        use manguebyte_dsp::{Partial, PartialBank};
        let (c, s) = (0.999_314_4_f32, 0.037_018_02_f32);
        let (pl, pr) = equal_power_pan(-0.3);
        let mut bank = PartialBank::new(16);
        bank.set_sample_rate(48000.0);
        bank.set_len(1);
        bank.set(
            0,
            Partial {
                freq_hz: 0.0,
                gain: 0.0,
                pan: 0.0,
            },
        );
        bank.set_rotation(0, c, s);
        bank.set_pan_gains(0, pl, pr);
        let start = random_phasor(0xBEEF);
        bank.set_phasor(0, start.cos, start.sin);
        let (mut osc, mut gain) = (start, 0.0f32);
        // Blocks of 64: the kernel's normalisation interval, so both take the
        // same steps at the same frames.
        for block in 0..8 {
            let (mut a, mut b) = (vec![0.0f32; 64], vec![0.0f32; 64]);
            let (mut x, mut y) = (vec![0.0f32; 64], vec![0.0f32; 64]);
            let target = 0.3 + 0.1 * block as f32;
            bank.ramp_to(0, target, 64);
            bank.render_add(&mut a, &mut b);
            // The Newton step 1.x took before each block, which the kernel takes
            // every 64 frames rendered.
            let mag = osc.cos * osc.cos + osc.sin * osc.sin;
            let renorm = 1.5 - 0.5 * mag;
            osc.cos *= renorm;
            osc.sin *= renorm;
            let inc = (target - gain) * (1.0 / 64.0);
            partial_accumulate(&mut x, &mut y, &mut osc, c, s, &mut gain, inc, pl, pr);
            assert_eq!(a, x, "left, bit for bit");
            assert_eq!(b, y, "right, bit for bit");
        }
    }

    #[test]
    fn pan_is_equal_power() {
        for i in 0..=40 {
            let (l, r) = equal_power_pan(i as f32 / 20.0 - 1.0);
            assert!((l * l + r * r - 1.0).abs() < 2e-5);
        }
        let (l, r) = equal_power_pan(0.0);
        assert!((l - r).abs() < 1e-6);
    }
}
