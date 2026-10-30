//! What is drawn.

use caecilia_model::{CouplerId, StopId};

/// The most stops a registration mask can hold: the host's stop-parameter
/// pool is exactly this wide, so a registration crosses to the audio thread as
/// one word.
pub const MASK_CAPACITY: usize = 64;

/// An ordered set of stops, sorted and unique.
#[derive(Clone, Debug, Default, PartialEq, Eq, Hash)]
pub struct StopSet(Vec<StopId>);

impl StopSet {
    /// The empty set.
    pub fn new() -> Self {
        Self::default()
    }

    /// The stops in `mask`, bit `n` being stop `n`.
    pub fn from_mask(mask: u64) -> Self {
        Self(
            (0..MASK_CAPACITY as u16)
                .filter(|b| mask & (1 << b) != 0)
                .map(StopId)
                .collect(),
        )
    }

    /// As a mask; stops past [`MASK_CAPACITY`] are left out.
    pub fn to_mask(&self) -> u64 {
        self.0
            .iter()
            .filter(|s| s.index() < MASK_CAPACITY)
            .fold(0, |m, s| m | 1 << s.0)
    }

    /// Whether `id` is in the set.
    pub fn contains(&self, id: StopId) -> bool {
        self.0.binary_search(&id).is_ok()
    }

    /// Add `id`; whether it was new.
    pub fn insert(&mut self, id: StopId) -> bool {
        match self.0.binary_search(&id) {
            Ok(_) => false,
            Err(i) => {
                self.0.insert(i, id);
                true
            }
        }
    }

    /// Remove `id`; whether it was there.
    pub fn remove(&mut self, id: StopId) -> bool {
        match self.0.binary_search(&id) {
            Ok(i) => {
                self.0.remove(i);
                true
            }
            Err(_) => false,
        }
    }

    /// Flip `id`; whether it is now in.
    pub fn toggle(&mut self, id: StopId) -> bool {
        if self.remove(id) {
            false
        } else {
            self.insert(id);
            true
        }
    }

    /// `self |= other`.
    pub fn unite(&mut self, other: &StopSet) {
        for &s in &other.0 {
            self.insert(s);
        }
    }

    /// `self &= other`.
    pub fn intersect(&mut self, other: &StopSet) {
        self.0.retain(|s| other.contains(*s));
    }

    /// `self -= other`.
    pub fn subtract(&mut self, other: &StopSet) {
        self.0.retain(|s| !other.contains(*s));
    }

    /// The stops, ascending.
    pub fn ids(&self) -> &[StopId] {
        &self.0
    }

    /// How many.
    pub fn len(&self) -> usize {
        self.0.len()
    }

    /// Whether empty.
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}

impl FromIterator<StopId> for StopSet {
    fn from_iter<I: IntoIterator<Item = StopId>>(iter: I) -> Self {
        let mut v: Vec<StopId> = iter.into_iter().collect();
        v.sort();
        v.dedup();
        Self(v)
    }
}

/// The drawn stops and couplers.
#[derive(Clone, Debug, Default, PartialEq, Eq, Hash)]
pub struct RegistrationState {
    /// Drawn stops.
    pub stops: StopSet,
    /// Drawn couplers, sorted and unique.
    pub couplers: Vec<CouplerId>,
}

impl RegistrationState {
    /// Whether `stop` is drawn.
    pub fn is_engaged(&self, stop: StopId) -> bool {
        self.stops.contains(stop)
    }

    /// Whether `coupler` is drawn.
    pub fn is_coupler_engaged(&self, coupler: CouplerId) -> bool {
        self.couplers.binary_search(&coupler).is_ok()
    }

    /// Draw a coupler.
    pub fn engage_coupler(&mut self, c: CouplerId) {
        if let Err(i) = self.couplers.binary_search(&c) {
            self.couplers.insert(i, c);
        }
    }

    /// Retire a coupler.
    pub fn disengage_coupler(&mut self, c: CouplerId) {
        if let Ok(i) = self.couplers.binary_search(&c) {
            self.couplers.remove(i);
        }
    }

    /// Flip a coupler.
    pub fn toggle_coupler(&mut self, c: CouplerId) {
        if self.is_coupler_engaged(c) {
            self.disengage_coupler(c);
        } else {
            self.engage_coupler(c);
        }
    }
}

/// What changes between two states.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct StateDelta {
    /// Stops to draw.
    pub engage: Vec<StopId>,
    /// Stops to retire.
    pub disengage: Vec<StopId>,
    /// Couplers to draw.
    pub engage_couplers: Vec<CouplerId>,
    /// Couplers to retire.
    pub disengage_couplers: Vec<CouplerId>,
}

impl StateDelta {
    /// `to` relative to `from`.
    pub fn between(from: &RegistrationState, to: &RegistrationState) -> Self {
        let only =
            |a: &[StopId], b: &StopSet| a.iter().copied().filter(|s| !b.contains(*s)).collect();
        let only_c = |a: &[CouplerId], b: &RegistrationState| {
            a.iter()
                .copied()
                .filter(|c| !b.is_coupler_engaged(*c))
                .collect()
        };
        Self {
            engage: only(to.stops.ids(), &from.stops),
            disengage: only(from.stops.ids(), &to.stops),
            engage_couplers: only_c(&to.couplers, from),
            disengage_couplers: only_c(&from.couplers, to),
        }
    }

    /// Whether nothing changes.
    pub fn is_empty(&self) -> bool {
        self.engage.is_empty()
            && self.disengage.is_empty()
            && self.engage_couplers.is_empty()
            && self.disengage_couplers.is_empty()
    }
}
