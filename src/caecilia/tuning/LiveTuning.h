// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/ITuning.h"
#include "caecilia/core/TripleBuffer.h"
#include "caecilia/tuning/DetuneCurve.h"
#include "caecilia/tuning/TuningTable.h"

#include <array>

/**
 * @file LiveTuning.h
 * @brief A tuning an organist can change while the instrument is sounding.
 *
 * @c TuningModel is the right thing to compute a temperament with and the wrong
 * thing to hand the audio thread: rebuilding it writes 128 doubles in place, and a
 * note-on reading the table at that moment is a data race, not merely a wrong
 * pitch. So the model stays off-thread and publishes a POD SNAPSHOT of what a
 * note-on actually consults.
 *
 * This is why the host's Temperament and Tuning A4 parameters did nothing at all
 * for so long. They were exposed, automatable and saved in the document, the
 * temperament library was implemented and unit-tested, and the only missing piece
 * was a safe way to get a rebuilt table across the thread boundary — so the
 * instrument played equal temperament at A=440 whatever the parameters said.
 */
namespace caecilia::tuning
{

/**
 * @brief Everything a note-on needs from the tuning, as plain data.
 *
 * Trivially copyable on purpose: it crosses the message/audio boundary by value
 * through a @c core::TripleBuffer, so it may not own anything. A @c TuningModel
 * cannot make that trip — it is polymorphic, so it is not trivially copyable, and
 * the buffer's static_assert says so.
 */
struct TuningSnapshot
{
    /// 8' unison Hz per MIDI note, with the temperament and any stretch baked in.
    std::array<double, TuningTable::kNoteCount> unisonHz{};

    /// Per-pipe scatter, applied on top. Zero-amplitude by default.
    DetuneCurve detune{};

    core::TemperamentId temperament = core::TemperamentId::Equal;
    double              a4Hz        = 440.0;
};

/**
 * @brief An @c ITuning whose table can be replaced while notes are sounding.
 *
 * Its address is stable for the life of the instrument, which is what lets every
 * voice hold a pointer to it forever — the pointer never has to be re-pointed, so
 * changing temperament does not mean walking five hundred voices.
 *
 * ## Threads
 * - @ref publish is OFF-thread. It hands a freshly built snapshot over.
 * - @ref adoptPending is the AUDIO thread, once per block, before any note-on is
 *   drained. It is the only place @c current_ is written, and the audio thread is
 *   the only thread that writes it — so every ITuning query below is an ordinary
 *   single-threaded read of memory nobody else touches.
 *
 * That split is the whole design: the expensive, allocating, 128-exp2 rebuild
 * happens where it is allowed to, and the audio thread pays one 1 KB copy on the
 * block where a temperament actually changed and nothing on any other block.
 */
class LiveTuning final : public core::ITuning
{
public:
    LiveTuning() noexcept;

    /**
     * @brief Hand a newly built snapshot to the audio thread. Off-thread only.
     *
     * Wait-free and does not block the caller; the audio thread picks it up on its
     * next block. Publishing twice before the audio thread looks is fine — it will
     * see the newer one, which is what an organist twisting a knob means.
     */
    void publish(const TuningSnapshot& snapshot) noexcept;

    /**
     * @brief Adopt anything published since the last block. Audio thread only.
     *
     * Costs nothing on a block where nothing changed: the triple buffer is asked
     * whether there is anything fresh before the copy is made.
     */
    void adoptPending() noexcept;

    /// The snapshot currently sounding. Audio thread.
    [[nodiscard]] const TuningSnapshot& current() const noexcept { return current_; }

    // --- core::ITuning ------------------------------------------------------

    [[nodiscard]] core::TemperamentId temperament() const noexcept override;
    [[nodiscard]] double referenceA4Hz() const noexcept override;
    [[nodiscard]] double frequencyForNote(core::MidiNote note) const noexcept override;
    [[nodiscard]] double frequencyForPipe(core::PipeId pipe,
                                          core::Footage footage) const noexcept override;

private:
    /// Audio-thread-owned. Written only by adoptPending().
    TuningSnapshot current_{};

    core::TripleBuffer<TuningSnapshot> pending_{};
};

/**
 * @brief Build a snapshot for a temperament and reference pitch. NOT RT-safe.
 *
 * 128 exp2 calls. Off the audio thread, and the reason this returns data rather
 * than mutating anything.
 */
[[nodiscard]] TuningSnapshot makeSnapshot(core::TemperamentId temperament,
                                          double              referenceA4Hz,
                                          DetuneCurve         detune = DetuneCurve::none());

} // namespace caecilia::tuning
