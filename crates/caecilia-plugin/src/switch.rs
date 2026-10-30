//! Changing the organ while the instrument plays.
//!
//! An organ is read and its engine built off the audio thread, then handed
//! over through a ring. The audio thread takes it at the top of a block: the
//! keys held at that moment sound on the new organ, the old one fades out
//! over [`FADE_SECONDS`], and the old engine goes back through a second ring
//! to be dropped here, since dropping an engine stops its worker threads.
//!
//! The slot keeps the organ of record and the file it came from, which the
//! session saves; `prepare` builds its engine from that organ, so an organ
//! chosen before the plugin is activated, or while it is re-activated, is
//! the one it plays.

use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, MutexGuard};

use caecilia_engine::{DEFAULT_POLYPHONY, OrganEngine, PoolConfig};
use caecilia_model::{
    Calibration, Diagnostic, DivisionId, MAX_STOPS, Organ, OrganFileError, read_organ_file,
};
use manguebyte_core::spsc::{Consumer, Producer, channel};

use crate::params::MAX_COUPLERS;

/// How long the organ being left takes to fall silent.
pub const FADE_SECONDS: f64 = 0.05;

/// Organs that can wait for the audio thread at once.
const IN_FLIGHT: usize = 4;

/// An organ on its way to the audio thread, and on its way back.
pub(crate) struct Swap {
    pub(crate) organ: Arc<Organ>,
    /// None when nothing was prepared yet: `prepare` builds it.
    pub(crate) engine: Option<OrganEngine>,
    /// The masks to draw as it takes over; None keeps the parameters as they
    /// are (a session restoring its own registration).
    pub(crate) registration: Option<(u64, u32)>,
    /// Which switch this is, so the history ignores what the organ before it
    /// played.
    pub(crate) generation: u32,
}

/// The audio thread's ends of the rings.
pub(crate) struct SwitchAudio {
    pub(crate) incoming: Consumer<Box<Swap>>,
    pub(crate) garbage: Producer<Box<Swap>>,
}

/// Why an organ was not taken.
#[derive(Debug)]
pub enum OrganLoadError {
    /// The file did not load.
    File(OrganFileError),
    /// More stops or couplers than the host parameters hold.
    TooLarge {
        /// Its stops.
        stops: usize,
        /// Its couplers.
        couplers: usize,
    },
    /// The audio thread has not taken the organs already sent; try again.
    Busy,
}

impl std::fmt::Display for OrganLoadError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::File(e) => write!(f, "{e}"),
            Self::TooLarge { stops, couplers } => write!(
                f,
                "{stops} stops and {couplers} couplers; Caecilia holds {MAX_STOPS} and {MAX_COUPLERS}"
            ),
            Self::Busy => write!(f, "the organs already chosen are still being taken"),
        }
    }
}

impl std::error::Error for OrganLoadError {}

/// The organ an instance plays, and the way to change it.
pub struct OrganSlot {
    inner: Mutex<Inner>,
}

struct Inner {
    organ: Arc<Organ>,
    path: Option<PathBuf>,
    /// (sample rate, largest block), once prepared.
    spec: Option<(f64, usize)>,
    generation: u32,
    /// What the last switch drew, for a history rebuilt before the audio
    /// thread has drawn it.
    registration: Option<(u64, u32)>,
    outgoing: Producer<Box<Swap>>,
    garbage: Consumer<Box<Swap>>,
}

impl Inner {
    /// Drop the organs the audio thread is done with.
    fn collect(&mut self) {
        while self.garbage.pop().is_some() {}
    }
}

/// An engine for `organ`, as the plugin plays one: every core but the audio
/// thread's renders voices, and a worker may hold a job for at most half the
/// block before the audio thread gives up on it (a late group is a block of
/// silence for its voices, a late callback a click for everything).
pub(crate) fn build_engine(organ: &Organ, sample_rate: f64, max_block: usize) -> OrganEngine {
    let block = std::time::Duration::from_secs_f64(max_block as f64 / sample_rate.max(1.0));
    OrganEngine::with_config(
        organ,
        sample_rate,
        max_block,
        DEFAULT_POLYPHONY,
        Calibration::V2,
        PoolConfig::all_cores().wait_limit(block / 2),
    )
}

/// The division each MIDI channel plays: the manuals say which, and a
/// channel no manual claims plays the primary one.
pub(crate) fn channels_of(organ: &Organ) -> [DivisionId; 16] {
    let mut division_of_channel = [organ.primary_manual(); 16];
    for m in organ.manuals() {
        if let Some(slot) = usize::try_from(m.midi_channel)
            .ok()
            .and_then(|c| division_of_channel.get_mut(c))
        {
            *slot = m.division;
        }
    }
    division_of_channel
}

/// A path as the session keeps it: absolute, so it survives a change of
/// working directory.
fn absolute(path: &Path) -> PathBuf {
    std::path::absolute(path).unwrap_or_else(|_| path.to_path_buf())
}

impl OrganSlot {
    pub(crate) fn new(organ: Arc<Organ>, path: Option<PathBuf>) -> (Self, SwitchAudio) {
        let (outgoing, incoming) = channel(IN_FLIGHT);
        let (garbage_in, garbage) = channel(IN_FLIGHT);
        let slot = Self {
            inner: Mutex::new(Inner {
                organ,
                path: path.as_deref().map(absolute),
                spec: None,
                generation: 0,
                registration: None,
                outgoing,
                garbage,
            }),
        };
        (
            slot,
            SwitchAudio {
                incoming,
                garbage: garbage_in,
            },
        )
    }

    fn lock(&self) -> MutexGuard<'_, Inner> {
        // A panic elsewhere while switching leaves a usable slot behind.
        self.inner.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// The organ of record: the one playing, or about to.
    pub fn organ(&self) -> Arc<Organ> {
        Arc::clone(&self.lock().organ)
    }

    /// The file it came from; None for the demonstration organ or one built
    /// in memory.
    pub fn path(&self) -> Option<PathBuf> {
        self.lock().path.clone()
    }

    /// How many times the organ has changed.
    pub fn generation(&self) -> u32 {
        self.lock().generation
    }

    /// The organ of record, its generation, and what its switch drew, read
    /// together.
    pub(crate) fn record(&self) -> (Arc<Organ>, u32, Option<(u64, u32)>) {
        let inner = self.lock();
        (
            Arc::clone(&inner.organ),
            inner.generation,
            inner.registration,
        )
    }

    /// Drop the organs the audio thread is done with. Never the audio thread.
    pub fn collect(&self) {
        self.lock().collect();
    }

    /// Send `organ` to the audio thread, its engine built here.
    pub(crate) fn switch(
        &self,
        organ: Organ,
        path: Option<&Path>,
        registration: Option<(u64, u32)>,
    ) -> Result<(), OrganLoadError> {
        if organ.stops().len() > MAX_STOPS || organ.couplers().len() > MAX_COUPLERS {
            return Err(OrganLoadError::TooLarge {
                stops: organ.stops().len(),
                couplers: organ.couplers().len(),
            });
        }
        // Held across the build, so `prepare` cannot slip in between and
        // leave an engine built for the wrong rate.
        let mut inner = self.lock();
        inner.collect();
        if inner.outgoing.free_len() == 0 {
            return Err(OrganLoadError::Busy);
        }
        let engine = inner
            .spec
            .map(|(sr, block)| build_engine(&organ, sr, block));
        let organ = Arc::new(organ);
        let generation = inner.generation.wrapping_add(1);
        let swap = Box::new(Swap {
            organ: Arc::clone(&organ),
            engine,
            registration,
            generation,
        });
        if inner.outgoing.push(swap).is_err() {
            return Err(OrganLoadError::Busy);
        }
        inner.organ = organ;
        inner.path = path.map(absolute);
        inner.generation = generation;
        inner.registration = registration;
        Ok(())
    }

    /// Read an organ file; its warnings come back with it.
    pub(crate) fn read(path: &Path) -> Result<(Organ, Vec<Diagnostic>), OrganLoadError> {
        let doc = read_organ_file(path).map_err(OrganLoadError::File)?;
        Ok((doc.organ, doc.warnings))
    }

    /// `prepare`'s side: note the new rate and block, throw away what was
    /// sent for the old ones, and return the organ to build for, its
    /// generation, and the registration the last discarded switch drew.
    pub(crate) fn prepare(
        &self,
        audio: &mut SwitchAudio,
        sample_rate: f64,
        max_block: usize,
    ) -> (Arc<Organ>, u32, Option<(u64, u32)>) {
        let mut inner = self.lock();
        inner.spec = Some((sample_rate, max_block));
        let mut registration = None;
        while let Some(swap) = audio.incoming.pop() {
            registration = swap.registration.or(registration);
        }
        inner.collect();
        (Arc::clone(&inner.organ), inner.generation, registration)
    }
}
