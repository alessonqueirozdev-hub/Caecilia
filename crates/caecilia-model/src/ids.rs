//! Identities and the semantic vocabulary every crate speaks.
//!
//! Every id is a dense index into the organ's own arrays, so a lookup is an
//! array access and costs nothing on the audio thread.

macro_rules! id {
    ($(#[$doc:meta])* $name:ident) => {
        $(#[$doc])*
        #[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
        pub struct $name(pub u16);

        impl $name {
            /// The index this id names.
            pub const fn index(self) -> usize {
                self.0 as usize
            }
        }
    };
}

id!(
    /// A rank of pipes.
    RankId
);
id!(
    /// A drawstop.
    StopId
);
id!(
    /// A keyboard section (Grand-Orgue, Récit, Pédale...).
    DivisionId
);
id!(
    /// A windchest: the wind supply a group of ranks stands on.
    WindchestId
);
id!(
    /// A coupler.
    CouplerId
);

/// MIDI note number.
pub type MidiNote = u8;

/// One physical pipe: its rank, its key and the division that sounds it.
///
/// The division is part of the identity. Without it the same key on two
/// manuals would be the same pipe, a note-off on one would release the other,
/// and two voices sounding "the same" pipe would be seeded identically and sum
/// coherently at +6 dB instead of +3.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct PipeId {
    /// Owning rank.
    pub rank: u16,
    /// The key.
    pub note: MidiNote,
    /// Owning division.
    pub division: u8,
}

/// Tonal family. The discriminants are those of 1.x and are hashed into the
/// voicing seeds, so they must never be renumbered.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum TonalFamily {
    /// Diapason / principal chorus.
    #[default]
    Principal = 0,
    /// Open and stopped flutes.
    Flute = 1,
    /// Narrow-scale strings.
    String = 2,
    /// Beating and free reeds.
    Reed = 3,
    /// Multi-rank compound stops.
    Mixture = 4,
    /// Cross-family voices.
    Hybrid = 5,
    /// Struck or plucked voices.
    Percussion = 6,
    /// Unknown.
    Undefined = 7,
}

/// The part a stop plays in a registration.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum ChorusRole {
    /// 16'/8' body.
    #[default]
    Foundation = 0,
    /// 4'/2' upperwork below the crown.
    Chorus = 1,
    /// Mixtures topping the chorus.
    MixtureCrown = 2,
    /// Exposed melodic voice.
    Solo = 3,
    /// Characterful colour.
    Color = 4,
    /// Zimbelstern, birdsong and the like.
    Effect = 5,
    /// Unknown.
    Undefined = 6,
}

/// Pitch class of a stop relative to 8'.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum PitchClass {
    /// Below unison (32', 16').
    Sub = 0,
    /// 8'.
    #[default]
    Unison = 1,
    /// 4'.
    Octave = 2,
    /// 2' and above.
    Super = 3,
    /// Non-octave harmonics.
    Mutation = 4,
    /// Multi-rank.
    Compound = 5,
}
