//! The registration as the audio thread sees it.

use caecilia_model::{CouplerId, DivisionId, Organ, RankVoicing, StopId, WindchestId};
use caecilia_wind::rank_wind_flow;

/// Ranks a table can hold.
pub const MAX_RANKS: usize = 64;
/// Couplers a table can hold.
pub const MAX_COUPLERS: usize = 32;

/// One drawn rank.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct EngagedRank {
    /// The stop that draws it; also the rank id stamped into its pipes.
    pub stop: StopId,
    /// Whose keys sound it.
    pub division: DivisionId,
    /// The chest it stands on.
    pub chest: WindchestId,
    /// What one of its pipes draws at middle C, relative to an 8'.
    pub wind_flow: f32,
}

/// One drawn coupler.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct EngagedCoupler {
    /// Whose ranks are borrowed.
    pub from: DivisionId,
    /// Whose keys borrow them.
    pub to: DivisionId,
    /// Transposition.
    pub semitones: i16,
}

/// Drawn ranks and couplers, fixed-size and `Copy`, so a new registration
/// crosses to the audio thread as plain data. Ranks and couplers travel
/// together: a key's expansion needs both, and publishing them apart would let
/// them disagree for a block.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EngagedTable {
    pub(crate) ranks: [EngagedRank; MAX_RANKS],
    rank_count: usize,
    couplers: [EngagedCoupler; MAX_COUPLERS],
    coupler_count: usize,
}

impl Default for EngagedTable {
    fn default() -> Self {
        Self {
            ranks: [EngagedRank::default(); MAX_RANKS],
            rank_count: 0,
            couplers: [EngagedCoupler::default(); MAX_COUPLERS],
            coupler_count: 0,
        }
    }
}

impl EngagedTable {
    /// The table for `stops` and `couplers` on `organ`. Stops without a voicing
    /// and anything past capacity are left out. Off the audio thread.
    pub fn build(
        organ: &Organ,
        voicings: &[RankVoicing],
        stops: &[StopId],
        couplers: &[CouplerId],
    ) -> Self {
        let mut t = Self::default();
        for &s in stops {
            let Some(v) = voicings.get(s.index()) else {
                continue;
            };
            if v.spectrum.partials.is_empty() || t.rank_count >= MAX_RANKS {
                continue;
            }
            t.ranks[t.rank_count] = EngagedRank {
                stop: v.stop,
                division: v.division,
                chest: v.chest,
                wind_flow: rank_wind_flow(v.footage),
            };
            t.rank_count += 1;
        }
        for &c in couplers {
            let Some(c) = organ.couplers().get(c.index()) else {
                continue;
            };
            if t.coupler_count >= MAX_COUPLERS {
                break;
            }
            t.couplers[t.coupler_count] = EngagedCoupler {
                from: c.from,
                to: c.to,
                semitones: c.semitones as i16,
            };
            t.coupler_count += 1;
        }
        t
    }

    /// The drawn ranks.
    pub fn ranks(&self) -> &[EngagedRank] {
        &self.ranks[..self.rank_count]
    }

    /// The drawn couplers.
    pub fn couplers(&self) -> &[EngagedCoupler] {
        &self.couplers[..self.coupler_count]
    }
}
