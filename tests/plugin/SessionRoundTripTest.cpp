// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief A session gives back what it was saved with.
 *
 * The session document carries sixteen properties plus the host parameters, and
 * every one of them is a chance to write something the restore does not read, to
 * read something in the wrong order, or to apply it somewhere that a later step
 * overwrites. Checking those one accessor at a time would cover only the ones with
 * an accessor -- the console trims, the reverb space and the master gain have
 * none, because nothing but the session ever needed to read them back.
 *
 * So the invariant is stated on the document instead: save, restore into a fresh
 * processor, save again, and the two documents must be identical. That reaches
 * everything the session carries, including whatever is added to it later, and it
 * is exactly what a host does with the bytes.
 */

#include "caecilia/plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

using namespace caecilia;
using caecilia::plugin::CaeciliaAudioProcessor;

namespace
{

struct JuceScope
{
    juce::ScopedJuceInitialiser_GUI gui;
};

/// Move every console control off its default.
///
/// A property that is never changed round-trips whether or not the restore reads
/// it, so a test built on defaults proves nothing. Each value here is one no
/// factory setting produces.
void deranged(CaeciliaAudioProcessor& processor)
{
    processor.setUiMaster(0.63f);
    processor.setUiVolume(0.41f);
    processor.setUiReverb(4, 0.77f);
    processor.setUiEqEnabled(false);
    processor.setUiTremulant(true);
    processor.setSeqNav(31, 97, true);

    // A registration that is not the opening one, and a coupler drawn.
    processor.toggleStop(core::StopId{ 2 });
    processor.toggleStop(core::StopId{ 5 });
    processor.toggleCoupler(1);
}

juce::MemoryBlock save(CaeciliaAudioProcessor& processor)
{
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    return block;
}

} // namespace

TEST_CASE("A restored session saves the same document it was given", "[plugin][session]")
{
    JuceScope scope;

    CaeciliaAudioProcessor first;
    deranged(first);
    const juce::MemoryBlock saved = save(first);

    CaeciliaAudioProcessor second;
    second.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

    // Byte-for-byte. Both documents are written by the same writer, and the second
    // one's values came from parsing the first, so anything the restore drops,
    // reorders or overwrites shows up here as a difference -- including in the
    // properties that no accessor exposes.
    const juce::MemoryBlock resaved = save(second);
    CHECK(resaved.getSize() == saved.getSize());
    CHECK(resaved == saved);
}

TEST_CASE("The observable console survives the round trip", "[plugin][session]")
{
    JuceScope scope;

    CaeciliaAudioProcessor first;
    deranged(first);
    const juce::MemoryBlock saved = save(first);

    CaeciliaAudioProcessor second;
    second.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

    // The document check above is the strong one, but it says "different" without
    // saying what. These name the pieces that do have accessors, so a regression
    // arrives with a diagnosis instead of a byte count.
    CHECK(second.drawnStops()      == first.drawnStops());
    CHECK(second.drawnCouplers()   == first.drawnCouplers());
    CHECK(second.seqPrevNote()     == first.seqPrevNote());
    CHECK(second.seqNextNote()     == first.seqNextNote());
    CHECK(second.seqNavEnabled()   == first.seqNavEnabled());
    CHECK(second.uiEqEnabled()     == first.uiEqEnabled());
    CHECK(second.uiTremulant()     == first.uiTremulant());
    CHECK(second.playDivision().value == first.playDivision().value);
}

TEST_CASE("A document that cannot be read changes nothing", "[plugin][session]")
{
    JuceScope scope;

    // Empty bytes, and bytes that are not this plugin's state at all. Hosts hand
    // back both: a corrupted project, a preset saved by something else, a chunk
    // that never arrived. None of them is permission to wipe a console the
    // organist has already set -- refusing to read is not the same as reading a
    // request to reset.
    CaeciliaAudioProcessor processor;
    deranged(processor);

    const std::uint64_t stops   = processor.drawnStops();
    const std::uint32_t couplrs = processor.drawnCouplers();
    const int           seqPrev = processor.seqPrevNote();

    processor.setStateInformation("", 0);
    const char foreign[] = "this is not a Caecilia session";
    processor.setStateInformation(foreign, static_cast<int>(sizeof(foreign) - 1));

    CHECK(processor.drawnStops()    == stops);
    CHECK(processor.drawnCouplers() == couplrs);
    CHECK(processor.seqPrevNote()   == seqPrev);
}
