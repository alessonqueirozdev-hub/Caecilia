//! The console's memory: general pistons and MIDI-learned controls.
//!
//! Both live beside the host parameters, in atomics, so the audio thread
//! reads and fires them without a lock, the controller edits them from any
//! other thread, and they are saved with the session as the parameters'
//! extra state.

use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};

/// General pistons: the first eight are the factory crescendo, the rest the
/// organist's own.
pub const GENERALS: usize = 16;

/// MIDI-learned controls the console can hold.
pub const LEARN_SLOTS: usize = 64;

/// What a learned control does.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    /// Flip a drawstop.
    ToggleStop(u16),
    /// Flip a coupler.
    ToggleCoupler(u16),
    /// Recall a general piston.
    General(u16),
    /// The next general in the row (the sequencer's page turn).
    NextGeneral,
    /// The previous general.
    PreviousGeneral,
    /// Retire every stop and coupler.
    Cancel,
}

impl Action {
    fn pack(self) -> u32 {
        let (kind, index) = match self {
            Self::ToggleStop(n) => (1, n),
            Self::ToggleCoupler(n) => (2, n),
            Self::General(n) => (3, n),
            Self::NextGeneral => (4, 0),
            Self::PreviousGeneral => (5, 0),
            Self::Cancel => (6, 0),
        };
        (kind << 16) | u32::from(index)
    }

    fn unpack(v: u32) -> Option<Self> {
        let index = (v & 0xFFFF) as u16;
        Some(match (v >> 16) & 0x7 {
            1 => Self::ToggleStop(index),
            2 => Self::ToggleCoupler(index),
            3 => Self::General(index),
            4 => Self::NextGeneral,
            5 => Self::PreviousGeneral,
            6 => Self::Cancel,
            _ => return None,
        })
    }
}

/// What a control is: a controller number or a key, on a channel.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Source {
    /// A control change.
    Controller {
        /// MIDI channel, 0-15.
        channel: u8,
        /// Controller number.
        number: u8,
    },
    /// A key: a toe stud, a piston keyboard, a spare key.
    Key {
        /// MIDI channel, 0-15.
        channel: u8,
        /// Key number.
        note: u8,
    },
}

impl Source {
    fn pack(self) -> u32 {
        let (kind, channel, number) = match self {
            Self::Controller { channel, number } => (1u32, channel, number),
            Self::Key { channel, note } => (2u32, channel, note),
        };
        (kind << 30) | (u32::from(channel & 0x0F) << 26) | (u32::from(number & 0x7F) << 19)
    }

    fn unpack(v: u32) -> Option<Self> {
        let channel = ((v >> 26) & 0x0F) as u8;
        let number = ((v >> 19) & 0x7F) as u8;
        match v >> 30 {
            1 => Some(Self::Controller { channel, number }),
            2 => Some(Self::Key {
                channel,
                note: number,
            }),
            _ => None,
        }
    }
}

/// A learned control: what it is and what it does.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Binding {
    /// The control.
    pub source: Source,
    /// What it does.
    pub action: Action,
}

impl Binding {
    fn pack(self) -> u32 {
        self.source.pack() | self.action.pack()
    }

    fn unpack(v: u32) -> Option<Self> {
        Some(Self {
            source: Source::unpack(v)?,
            action: Action::unpack(v & 0x7_FFFF)?,
        })
    }
}

/// The console's memory. Every method is lock-free and allocation-free
/// except the text form, which is for saving.
pub struct ConsoleMemory {
    general_stops: [AtomicU64; GENERALS],
    general_couplers: [AtomicU32; GENERALS],
    current_general: AtomicU32,
    learn: [AtomicU32; LEARN_SLOTS],
    armed: AtomicU32,
}

impl Default for ConsoleMemory {
    fn default() -> Self {
        Self {
            general_stops: std::array::from_fn(|_| AtomicU64::new(0)),
            general_couplers: std::array::from_fn(|_| AtomicU32::new(0)),
            current_general: AtomicU32::new(u32::MAX),
            learn: std::array::from_fn(|_| AtomicU32::new(0)),
            armed: AtomicU32::new(0),
        }
    }
}

impl ConsoleMemory {
    /// What general `n` draws, as (stops, couplers) masks.
    pub fn general(&self, n: usize) -> Option<(u64, u32)> {
        Some((
            self.general_stops.get(n)?.load(Ordering::Relaxed),
            self.general_couplers[n].load(Ordering::Relaxed),
        ))
    }

    /// Set general `n`.
    pub fn set_general(&self, n: usize, stops: u64, couplers: u32) {
        if n < GENERALS {
            self.general_stops[n].store(stops, Ordering::Relaxed);
            self.general_couplers[n].store(couplers, Ordering::Relaxed);
        }
    }

    /// The general last recalled, if any.
    pub fn current_general(&self) -> Option<usize> {
        let v = self.current_general.load(Ordering::Relaxed);
        (v != u32::MAX).then_some(v as usize)
    }

    pub(crate) fn set_current_general(&self, n: usize) {
        self.current_general.store(n as u32, Ordering::Relaxed);
    }

    /// Arm learning: the next control the organist moves is bound to `action`.
    pub fn arm(&self, action: Action) {
        self.armed.store(action.pack() | 1 << 31, Ordering::Relaxed);
    }

    /// Stop waiting for a control.
    pub fn disarm(&self) {
        self.armed.store(0, Ordering::Relaxed);
    }

    /// What learning is waiting to bind, if anything.
    pub fn armed(&self) -> Option<Action> {
        let v = self.armed.load(Ordering::Relaxed);
        if v == 0 {
            None
        } else {
            Action::unpack(v & 0x7_FFFF)
        }
    }

    /// Bind `source` to the armed action, replacing what `source` did
    /// before. Audio thread: returns whether a binding was made.
    pub(crate) fn capture(&self, source: Source) -> bool {
        let Some(action) = self.armed() else {
            return false;
        };
        self.armed.store(0, Ordering::Relaxed);
        self.bind(Binding { source, action })
    }

    /// Bind a control. Returns false when every slot is taken.
    pub fn bind(&self, binding: Binding) -> bool {
        let packed = binding.pack();
        let src = binding.source.pack();
        // The same control again replaces its old action.
        let slot = self
            .learn
            .iter()
            .position(|s| {
                let v = s.load(Ordering::Relaxed);
                v != 0 && v & 0xFFF8_0000 == src
            })
            .or_else(|| {
                self.learn
                    .iter()
                    .position(|s| s.load(Ordering::Relaxed) == 0)
            });
        match slot {
            Some(i) => {
                self.learn[i].store(packed, Ordering::Relaxed);
                true
            }
            None => false,
        }
    }

    /// What `source` is bound to. Audio thread.
    pub fn lookup(&self, source: Source) -> Option<Action> {
        let src = source.pack();
        self.learn
            .iter()
            .map(|s| s.load(Ordering::Relaxed))
            .find(|&v| v != 0 && v & 0xFFF8_0000 == src)
            .and_then(|v| Action::unpack(v & 0x7_FFFF))
    }

    /// Forget what `source` was bound to.
    pub fn unbind(&self, source: Source) {
        let src = source.pack();
        for s in &self.learn {
            let v = s.load(Ordering::Relaxed);
            if v != 0 && v & 0xFFF8_0000 == src {
                s.store(0, Ordering::Relaxed);
            }
        }
    }

    /// Forget every learned control.
    pub fn clear_bindings(&self) {
        for s in &self.learn {
            s.store(0, Ordering::Relaxed);
        }
    }

    /// Another organ: forget the controls bound to a stop or a coupler, which
    /// name them by number on the organ being left, and which general was
    /// last recalled. Pistons, the sequencer and cancel keep their controls.
    pub(crate) fn forget_organ(&self) {
        for s in &self.learn {
            let action = Binding::unpack(s.load(Ordering::Relaxed)).map(|b| b.action);
            if matches!(
                action,
                Some(Action::ToggleStop(_) | Action::ToggleCoupler(_))
            ) {
                s.store(0, Ordering::Relaxed);
            }
        }
        self.current_general.store(u32::MAX, Ordering::Relaxed);
        self.disarm();
    }

    /// Every learned control.
    pub fn bindings(&self) -> Vec<Binding> {
        self.learn
            .iter()
            .filter_map(|s| Binding::unpack(s.load(Ordering::Relaxed)))
            .collect()
    }

    /// The generals as text, one per line: `index stops couplers`, in hex.
    pub fn generals_text(&self) -> String {
        (0..GENERALS)
            .map(|n| {
                let (s, c) = self.general(n).unwrap_or_default();
                format!("{n} {s:x} {c:x}\n")
            })
            .collect()
    }

    /// Restore the generals from [`generals_text`](Self::generals_text).
    pub fn load_generals(&self, text: &str) -> bool {
        let mut any = false;
        for line in text.lines() {
            let mut f = line.split_whitespace();
            let (Some(n), Some(s), Some(c)) = (f.next(), f.next(), f.next()) else {
                continue;
            };
            if let (Ok(n), Ok(s), Ok(c)) = (
                n.parse::<usize>(),
                u64::from_str_radix(s, 16),
                u32::from_str_radix(c, 16),
            ) {
                self.set_general(n, s, c);
                any = true;
            }
        }
        any
    }

    /// The learned controls as text, one packed binding per line, in hex.
    pub fn bindings_text(&self) -> String {
        self.bindings()
            .iter()
            .map(|b| format!("{:08x}\n", b.pack()))
            .collect()
    }

    /// Restore the learned controls from [`bindings_text`](Self::bindings_text).
    pub fn load_bindings(&self, text: &str) -> bool {
        self.clear_bindings();
        let mut any = false;
        for line in text.lines() {
            if let Some(b) = u32::from_str_radix(line.trim(), 16)
                .ok()
                .and_then(Binding::unpack)
            {
                any |= self.bind(b);
            }
        }
        any
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bindings_round_trip_through_their_packed_and_text_forms() {
        let m = ConsoleMemory::default();
        let all = [
            Binding {
                source: Source::Controller {
                    channel: 3,
                    number: 80,
                },
                action: Action::ToggleStop(25),
            },
            Binding {
                source: Source::Key {
                    channel: 15,
                    note: 127,
                },
                action: Action::General(15),
            },
            Binding {
                source: Source::Controller {
                    channel: 0,
                    number: 0,
                },
                action: Action::NextGeneral,
            },
            Binding {
                source: Source::Key {
                    channel: 0,
                    note: 36,
                },
                action: Action::Cancel,
            },
        ];
        for b in all {
            assert!(m.bind(b));
            assert_eq!(m.lookup(b.source), Some(b.action));
        }
        let text = m.bindings_text();
        let n = ConsoleMemory::default();
        assert!(n.load_bindings(&text));
        assert_eq!(n.bindings(), m.bindings());
        // Binding a control again replaces it.
        assert!(n.bind(Binding {
            source: all[0].source,
            action: Action::ToggleCoupler(2)
        }));
        assert_eq!(n.bindings().len(), 4);
        assert_eq!(n.lookup(all[0].source), Some(Action::ToggleCoupler(2)));
    }
}
