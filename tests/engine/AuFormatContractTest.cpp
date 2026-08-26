// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Two properties that only the Audio Unit format ever exercises, pinned here
// because the format itself cannot be tested from this suite — the test target
// links caecilia::core and nothing else, and auval only exists on macOS.
//
//   1. A MONO output. Every other host we support asks for stereo, so the
//      channels > 1 guards scattered through the master chain are dead code in
//      practice — right up until Logic instantiates the mono variant, at which
//      point one missing guard is a write past the end of a buffer. They are
//      correct today (verified by reading every one); what they lack is anything
//      that would notice if a future edit dropped one.
//
//   2. Re-preparation. auval reconfigures the instrument repeatedly across sample
//      rates and block sizes and then checks it is silent, which is a harder
//      question than it sounds: every stage in this chain holds a tail, and a
//      prepare that resizes a delay line without clearing it hands the next
//      render whatever was in the old one.
//
// Worth running under the asan-ubsan preset, where an unguarded channel(1) stops
// being a silent overwrite and becomes a hard failure.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/dsp/FdnReverb.h"
#include "caecilia/dsp/Limiter.h"
#include "caecilia/dsp/MasterEq.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/SpectralModel.h"

#include "support/RankTable.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

namespace core   = caecilia::core;
namespace dsp    = caecilia::dsp;
namespace engine = caecilia::core::engine;
namespace synth  = caecilia::synth;

namespace
{
constexpr std::size_t kVoices = 8;

synth::SpectralModel tone()
{
    synth::SpectralModel m;
    for (int n = 1; n <= 6; ++n)
    {
        synth::PartialTrack t;
        t.ratioToF0 = static_cast<float>(n);
        t.ampDb     = -5.0f * static_cast<float>(n - 1);
        t.seed      = 0x2200u + static_cast<std::uint32_t>(n);
        m.partials.push_back(t);
    }
    m.fundamentalHz = 220.0f;
    return m;
}

/// The whole master chain at one channel count, rebuildable at will — which is
/// what auval spends most of its time doing.
struct MonoRig
{
    engine::AudioEngine                               engineInstance;
    caecilia::tests::RankTable                        ranks{ 1, 4 };
    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
    std::vector<core::IVoice*>                         ptrs;
    dsp::FdnReverb                                     reverb;
    dsp::MasterEq                                      eq;
    dsp::Limiter                                       limiter;

    explicit MonoRig(std::size_t channels)
    {
        synth::VoiceContext ctx;
        ctx.family  = core::TonalFamily::Principal;
        ctx.footage = core::footage::kEight;

        for (std::size_t i = 0; i < kVoices; ++i)
        {
            auto v = std::make_unique<synth::AdditiveVoice>();
            v->bank().setMaxPartials(8);
            v->setContext(ctx);
            ptrs.push_back(v.get());
            voices.push_back(std::move(v));
        }
        engineInstance.bindVoices(ptrs.data(), ptrs.size());
        engineInstance.setMasterReverb(&reverb);
        ranks.publishTo(engineInstance);
        (void) channels;
    }

    void prepare(double sampleRate, std::size_t block, std::size_t channels)
    {
        for (auto& v : voices)
        {
            v->prepare(sampleRate, block);
            v->seedFrom(tone());
        }
        engineInstance.prepare(sampleRate, block, channels, 1);
        reverb.prepare(sampleRate, block, channels);
        reverb.setPreset(dsp::ReverbPreset::Cathedral); // the longest tail we ship
        eq.prepare(sampleRate, block, channels);
        limiter.prepare(sampleRate, block, channels);
        limiter.setParams(-3.0f, 2.5f, 400.0f, 600.0f);
    }

    /// Render one block through every stage, exactly as the processor does.
    void render(std::vector<float>& mono, std::size_t frames)
    {
        float* chans[1] = { mono.data() };
        core::AudioBlock block(chans, 1, frames);
        engineInstance.processBlock(block);
        eq.process(block);
        limiter.process(block);
    }

    void send(const engine::EngineCommand& c)
    {
        (void) engineInstance.commandQueue().push(c);
    }
};
} // namespace

TEST_CASE("The master chain renders a single-channel bus", "[engine][au][format]")
{
    // AU is the only format that asks for mono, so every `channels > 1` guard in
    // the chain is unexercised until Logic loads the mono variant. They are
    // correct today; this is what will say so tomorrow.
    constexpr double      kSr    = 48000.0;
    constexpr std::size_t kBlock = 256;

    MonoRig rig(1);
    rig.prepare(kSr, kBlock, 1);
    rig.send(engine::EngineCommand::makeNoteOn(core::PipeId{ 0, 60, 0 }, 100,
                                               core::DivisionId{ 0 }, 0));

    std::vector<float> mono(kBlock, 0.0f);
    bool   allFinite = true;
    double energy    = 0.0;

    for (int b = 0; b < 40; ++b)
    {
        std::fill(mono.begin(), mono.end(), 0.0f);
        rig.render(mono, kBlock);
        for (const float s : mono)
        {
            allFinite = allFinite && std::isfinite(s);
            energy += static_cast<double>(s) * s;
        }
    }

    CHECK(allFinite);
    CHECK(energy > 0.0); // it is mono, not silent
}

TEST_CASE("Re-preparing across rates and block sizes leaves no tail",
          "[engine][au][format]")
{
    // What auval actually does: reconfigure, then check the instrument is silent
    // with no input. Every stage here holds a tail — a 5.2 s Cathedral reverb, a
    // limiter look-ahead ring, five biquads — so a prepare that resizes a buffer
    // without clearing it hands the next render whatever the old one contained.
    //
    // The rates deliberately go both up and down, and end where they started: a
    // buffer that is only ever grown hides the bug that a shrink exposes.
    const double      rates[]  = { 44100.0, 48000.0, 96000.0, 22050.0, 44100.0 };
    const std::size_t blocks[] = {    512,      64,    1024,      32,     256 };

    MonoRig rig(2);

    for (std::size_t i = 0; i < 5; ++i)
    {
        for (const std::size_t channels : { std::size_t{ 1 }, std::size_t{ 2 } })
        {
            rig.prepare(rates[i], blocks[i], channels);

            // Excite it, so there IS a tail to fail to clear.
            rig.send(engine::EngineCommand::makeNoteOn(core::PipeId{ 0, 55, 0 }, 100,
                                                       core::DivisionId{ 0 }, 0));
            std::vector<float> buf(blocks[i], 0.0f);
            for (int b = 0; b < 8; ++b)
            {
                std::fill(buf.begin(), buf.end(), 0.0f);
                rig.render(buf, blocks[i]);
            }

            // Reconfigure, and NOTHING else. Each stage's prepare() has to clear
            // its own buffers; calling reset() here as well would do that work for
            // them and the test would pass however they behaved.
            rig.prepare(rates[i], blocks[i], channels);

            std::vector<float> after(blocks[i], 0.0f);
            rig.render(after, blocks[i]);

            double peak = 0.0;
            bool   finite = true;
            for (const float s : after)
            {
                finite = finite && std::isfinite(s);
                peak = std::max(peak, std::abs(static_cast<double>(s)));
            }

            INFO("rate " << rates[i] << ", block " << blocks[i]
                         << ", channels " << channels << ", peak " << peak);
            CHECK(finite);
            CHECK(peak == 0.0);
        }
    }
}


TEST_CASE("A host reset silences the instrument immediately", "[engine][au][format]")
{
    // What a host means by reset: everything in flight is discarded, and the very
    // next block is silent. Not "starts fading", which is what an all-notes-off
    // does -- that leaves the instrument sounding at full level for the rest of
    // the block and audible for a third of a second afterwards.
    //
    // No reverb is bound here on purpose. The tail of the master reverb belongs to
    // whoever owns it; this measures what AudioEngine::reset() itself promises.
    constexpr double      kSr    = 48000.0;
    constexpr std::size_t kBlock = 256;

    MonoRig rig(2);
    rig.engineInstance.setMasterReverb(nullptr);
    rig.prepare(kSr, kBlock, 2);
    rig.engineInstance.setMasterReverb(nullptr); // prepare() does not rebind it

    for (std::uint8_t k = 0; k < 4; ++k)
        rig.send(engine::EngineCommand::makeNoteOn(
            core::PipeId{ 0, static_cast<std::uint8_t>(58 + k), 0 }, 100,
            core::DivisionId{ 0 }, 0));

    std::vector<float> buf(kBlock, 0.0f);
    double soundingPeak = 0.0;
    for (int b = 0; b < 20; ++b) // let the attack finish, so there IS something to cut
    {
        std::fill(buf.begin(), buf.end(), 0.0f);
        float* chans[1] = { buf.data() };
        core::AudioBlock block(chans, 1, kBlock);
        rig.engineInstance.processBlock(block);
        for (const float s : buf)
            soundingPeak = std::max(soundingPeak, std::abs(static_cast<double>(s)));
    }
    REQUIRE(soundingPeak > 1.0e-4); // it really was sounding
    REQUIRE(rig.engineInstance.activeVoiceCount() > 0);

    rig.engineInstance.reset();

    // The very next block, not the one after, and not a fade.
    std::fill(buf.begin(), buf.end(), 0.0f);
    float* chans[1] = { buf.data() };
    core::AudioBlock block(chans, 1, kBlock);
    rig.engineInstance.processBlock(block);

    double peak = 0.0;
    for (const float s : buf)
        peak = std::max(peak, std::abs(static_cast<double>(s)));

    INFO("peak after reset " << peak << " (was " << soundingPeak << ")");
    CHECK(peak == 0.0);
    CHECK(rig.engineInstance.activeVoiceCount() == 0);
}

TEST_CASE("A reset drops commands queued for a block that will never render",
          "[engine][au][format]")
{
    // A note-on enqueued just before the host reset would otherwise arrive on the
    // next block and un-silence an instrument that has been told to be silent --
    // which, in an offline render, means the take begins with a note nobody
    // played.
    MonoRig rig(2);
    rig.engineInstance.setMasterReverb(nullptr);
    rig.prepare(48000.0, 256, 2);
    rig.engineInstance.setMasterReverb(nullptr);

    rig.send(engine::EngineCommand::makeNoteOn(core::PipeId{ 0, 60, 0 }, 100,
                                               core::DivisionId{ 0 }, 0));
    rig.engineInstance.reset(); // before the command was ever drained

    std::vector<float> buf(256, 0.0f);
    for (int b = 0; b < 4; ++b)
    {
        std::fill(buf.begin(), buf.end(), 0.0f);
        float* chans[1] = { buf.data() };
        core::AudioBlock block(chans, 1, 256);
        rig.engineInstance.processBlock(block);
    }

    CHECK(rig.engineInstance.activeVoiceCount() == 0);
}
