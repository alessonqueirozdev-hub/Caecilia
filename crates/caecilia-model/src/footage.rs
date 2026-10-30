//! Exact-rational footage.
//!
//! A rank's footage is kept as a reduced fraction of feet, never as a float:
//! 8' is 8/1, 2 2/3' is 8/3, 1 3/5' is 8/5. That is a correctness requirement,
//! not a nicety. Whether a stop is a mutation -- a quint or a tierce that must
//! never be drawn into a principal chorus -- is a question about whether its
//! pitch ratio to 8' is a power of two, and a float cannot answer it.

/// A pipe length in feet, as a reduced fraction with a positive denominator.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Footage {
    num: i32,
    den: i32,
}

impl Default for Footage {
    fn default() -> Self {
        Self::EIGHT
    }
}

const fn gcd(mut a: i32, mut b: i32) -> i32 {
    while b != 0 {
        let t = a % b;
        a = b;
        b = t;
    }
    a
}

const fn is_power_of_two(v: i32) -> bool {
    v > 0 && (v & (v - 1)) == 0
}

const fn log2_exact(mut v: i32) -> i32 {
    let mut e = 0;
    while v > 1 {
        v >>= 1;
        e += 1;
    }
    e
}

impl Footage {
    /// 32'.
    pub const THIRTY_TWO: Self = Self::new(32, 1);
    /// 16'.
    pub const SIXTEEN: Self = Self::new(16, 1);
    /// 10 2/3', the quint of the 32' series.
    pub const TEN_AND_TWO_THIRDS: Self = Self::new(32, 3);
    /// 8', the unison.
    pub const EIGHT: Self = Self::new(8, 1);
    /// 5 1/3'.
    pub const FIVE_AND_THIRD: Self = Self::new(16, 3);
    /// 4'.
    pub const FOUR: Self = Self::new(4, 1);
    /// 2 2/3', the Nazard.
    pub const TWO_AND_TWO_THIRDS: Self = Self::new(8, 3);
    /// 2'.
    pub const TWO: Self = Self::new(2, 1);
    /// 1 3/5', the Tierce.
    pub const ONE_AND_THREE_FIFTHS: Self = Self::new(8, 5);
    /// 1 1/3', the Larigot.
    pub const ONE_AND_THIRD: Self = Self::new(4, 3);
    /// 1'.
    pub const ONE: Self = Self::new(1, 1);

    /// `num/den` feet, reduced. A zero denominator means 8'.
    pub const fn new(num: i32, den: i32) -> Self {
        let (mut n, mut d) = (num, den);
        if d < 0 {
            n = -n;
            d = -d;
        }
        if d == 0 {
            n = 8;
            d = 1;
        }
        let g = gcd(if n < 0 { -n } else { n }, d);
        if g != 0 {
            Self {
                num: n / g,
                den: d / g,
            }
        } else {
            Self { num: n, den: d }
        }
    }

    /// Numerator of the reduced fraction.
    pub const fn num(self) -> i32 {
        self.num
    }

    /// Denominator of the reduced fraction, always positive.
    pub const fn den(self) -> i32 {
        self.den
    }

    /// Length in feet.
    pub fn feet(self) -> f64 {
        f64::from(self.num) / f64::from(self.den)
    }

    /// The pitch ratio to 8' unison, `8 * den / num`, reduced.
    const fn ratio_to_unison_exact(self) -> (i32, i32) {
        let (mut rn, mut rd) = (8 * self.den, self.num);
        let g = gcd(if rn < 0 { -rn } else { rn }, if rd < 0 { -rd } else { rd });
        if g != 0 {
            rn /= g;
            rd /= g;
        }
        (rn, rd)
    }

    /// Pitch ratio to 8' unison as a float: 4' is 2, 2 2/3' is 3, 1 3/5' is 5.
    /// Zero for a zero length.
    pub fn ratio_to_unison(self) -> f32 {
        if self.num == 0 {
            return 0.0;
        }
        (8.0 * self.den as f32) / self.num as f32
    }

    /// Whether this sounds a non-octave harmonic of 8' (a quint, a tierce...).
    pub const fn is_mutation(self) -> bool {
        let (rn, rd) = self.ratio_to_unison_exact();
        !(is_power_of_two(rn) && is_power_of_two(rd))
    }

    /// Octaves above 8': 0 for 8', +1 for 4', -1 for 16'; 0 for a mutation.
    pub const fn octave_class_from_8(self) -> i32 {
        if self.is_mutation() || self.num == 0 {
            return 0;
        }
        let (rn, rd) = self.ratio_to_unison_exact();
        log2_exact(rn) - log2_exact(rd)
    }

    /// The nearest standard footage to a decimal length (2.667 -> 2 2/3').
    pub fn from_feet(feet: f64) -> Self {
        const TABLE: [(f64, Footage); 10] = [
            (32.0, Footage::THIRTY_TWO),
            (16.0, Footage::SIXTEEN),
            (16.0 / 3.0, Footage::FIVE_AND_THIRD),
            (8.0, Footage::EIGHT),
            (4.0, Footage::FOUR),
            (8.0 / 3.0, Footage::TWO_AND_TWO_THIRDS),
            (2.0, Footage::TWO),
            (8.0 / 5.0, Footage::ONE_AND_THREE_FIFTHS),
            (4.0 / 3.0, Footage::ONE_AND_THIRD),
            (1.0, Footage::ONE),
        ];
        let mut best = Footage::EIGHT;
        let mut best_err = f64::INFINITY;
        for (f, footage) in TABLE {
            let e = if feet > 0.0 {
                (feet / f).ln().abs()
            } else {
                (feet - f).abs()
            };
            if e < best_err {
                best_err = e;
                best = footage;
            }
        }
        best
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reduces_and_classifies() {
        assert_eq!(Footage::new(16, 6), Footage::new(8, 3));
        assert!(Footage::TWO_AND_TWO_THIRDS.is_mutation());
        assert!(Footage::ONE_AND_THREE_FIFTHS.is_mutation());
        assert!(!Footage::SIXTEEN.is_mutation());
        assert_eq!(Footage::SIXTEEN.octave_class_from_8(), -1);
        assert_eq!(Footage::TWO.octave_class_from_8(), 2);
        assert_eq!(Footage::TWO_AND_TWO_THIRDS.octave_class_from_8(), 0);
        assert_eq!(Footage::TWO_AND_TWO_THIRDS.ratio_to_unison(), 3.0);
        assert_eq!(Footage::from_feet(2.667), Footage::TWO_AND_TWO_THIRDS);
    }
}
