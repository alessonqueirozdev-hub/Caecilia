// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// ReverbSendGate. This logic used to sit inside the plugin's CommandBridge, where
// nothing could reach it — that target links JUCE and the test suite deliberately
// does not — and the plan for it said, in so many words, that the only way to
// check it was to automate a knob in a host and listen for a stall.
//
// Its awkward part is the settle counter. An epsilon gate has a tail: every
// sub-epsilon step it drops leaves the engine slightly behind, and at the end of a
// sweep the difference just stays there. The knob reads 3.00 s and the reverb
// sits at 2.99 forever. That is not something anyone hears; it is something a test
// can see exactly.
//

#include "caecilia/core/ReverbSendGate.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
namespace core = caecilia::core;

namespace
{
core::ReverbParams hall()
{
    core::ReverbParams p;
    p.mix        = 0.28f;
    p.decaySec   = 2.6f;
    p.preDelayMs = 18.0f;
    p.dampingHz  = 6500.0f;
    p.widthNorm  = 1.0f;
    p.bassBloom  = 1.35f;
    return p;
}
} // namespace

TEST_CASE("The reverb gate always sends the first request", "[core][reverb][gate]")
{
    // Until something has gone out, the gate has no idea what the engine holds,
    // and "no difference from the default-constructed set" is not the same claim
    // as "the engine already has this".
    core::ReverbSendGate gate;
    CHECK_FALSE(gate.primed());
    CHECK(gate.shouldSend(core::ReverbParams{}));
    CHECK(gate.primed());
}

TEST_CASE("The reverb gate drops a repeat of what it already sent",
          "[core][reverb][gate]")
{
    core::ReverbSendGate gate;
    const core::ReverbParams p = hall();
    REQUIRE(gate.shouldSend(p));

    // A host with an automation lane parked on a value sends it every block.
    for (int i = 0; i < 200; ++i)
        CHECK_FALSE(gate.shouldSend(p));
}

TEST_CASE("The reverb gate always forwards mix and pre-delay", "[core][reverb][gate]")
{
    // They cost the engine nothing, so there is no threshold to be under — but
    // they still have to arrive, and a gate written only around the expensive
    // fields would swallow them.
    core::ReverbSendGate gate;
    core::ReverbParams p = hall();
    REQUIRE(gate.shouldSend(p));

    p.mix += 0.001f;
    CHECK(gate.shouldSend(p));

    p.preDelayMs += 0.01f;
    CHECK(gate.shouldSend(p));
}

TEST_CASE("The reverb gate drops a sub-epsilon coefficient change",
          "[core][reverb][gate]")
{
    core::ReverbSendGate gate;
    core::ReverbParams p = hall();
    REQUIRE(gate.shouldSend(p));

    // Half an epsilon on each of the four coefficient-driving fields.
    p.decaySec  += 0.004f;
    p.dampingHz += 8.0f;
    p.widthNorm -= 0.002f;
    p.bassBloom += 0.004f;
    CHECK_FALSE(gate.shouldSend(p));
}

TEST_CASE("The reverb gate bounds the traffic of a sweep", "[core][reverb][gate]")
{
    // What the gate is for. A host dragging the decay knob delivers a new value
    // every block; every one of them used to become a command.
    core::ReverbSendGate gate;
    core::ReverbParams p = hall();
    p.decaySec = 2.0f;
    REQUIRE(gate.shouldSend(p));

    constexpr int kBlocks = 400;
    int sent = 0;
    for (int i = 1; i <= kBlocks; ++i)
    {
        p.decaySec = 2.0f + 2.0f * static_cast<float>(i) / static_cast<float>(kBlocks);
        if (gate.shouldSend(p))
            ++sent;
    }

    INFO(sent << " of " << kBlocks << " sent");
    // Two seconds of travel against a 0.01 s epsilon: roughly one send per epsilon
    // crossed, which lands near 150 rather than the 400 that used to go out. The
    // bounds are wide because whether a step of exactly one epsilon counts depends
    // on float rounding, and that is not what this is asserting.
    CHECK(sent < kBlocks / 2);
    CHECK(sent > kBlocks / 4);
}

TEST_CASE("The reverb gate closes the sweep's tail once the knob stops",
          "[core][reverb][gate][regression]")
{
    // The failure this exists to prevent, and the one nobody would find by ear.
    // The last step of a sweep is sub-epsilon, so it is dropped -- and then the
    // host stops moving, nothing further arrives, and the engine is left holding a
    // value that is not the one the knob shows. Permanently.
    core::ReverbSendGate gate;
    core::ReverbParams p = hall();
    p.decaySec = 2.0f;
    REQUIRE(gate.shouldSend(p));

    // Walk to 2.009 in steps far below the epsilon: none of these is worth sending.
    for (int i = 1; i <= 9; ++i)
    {
        p.decaySec = 2.0f + 0.001f * static_cast<float>(i);
        CHECK_FALSE(gate.shouldSend(p));
    }
    REQUIRE(gate.lastSent().decaySec == Approx(2.0f));

    // The knob stops. Within the settle window the exact value goes out, once.
    int sent = 0;
    for (int i = 0; i < core::ReverbSendGate::kSettleBlocks + 4; ++i)
        if (gate.shouldSend(p))
            ++sent;

    INFO("sent " << sent << " after settling, now at " << gate.lastSent().decaySec);
    CHECK(sent == 1);
    CHECK(gate.lastSent().decaySec == Approx(p.decaySec));
}

TEST_CASE("The reverb gate does not flush mid-sweep", "[core][reverb][gate]")
{
    // The settle counter must measure a genuine pause, not a momentarily flat
    // stretch of a moving value. Counted on what was SENT rather than on what
    // arrives, it would fire on every block a sub-epsilon sweep spent below the
    // threshold -- which is most of them, and the gate would do nothing at all.
    core::ReverbSendGate gate;
    core::ReverbParams p = hall();
    p.decaySec = 2.0f;
    REQUIRE(gate.shouldSend(p));

    // Forty blocks of motion whose TOTAL is still under one epsilon, so nothing
    // here is worth sending on its own account -- and the value is never still, so
    // the settle counter must never reach its threshold either.
    int sent = 0;
    for (int i = 1; i <= 40; ++i)
    {
        p.decaySec = 2.0f + 0.0002f * static_cast<float>(i); // 0.008 total
        if (gate.shouldSend(p))
            ++sent;
    }
    CHECK(sent == 0);
}

TEST_CASE("A forced send carries fields no diff can see", "[core][reverb][gate]")
{
    // The console publishes a whole space preset, bass bloom included -- and the
    // bloom has no host parameter, so if the fields the APVTS covers happen not to
    // have moved, a diff sees nothing and the space change is swallowed.
    core::ReverbSendGate gate;
    const core::ReverbParams p = hall();
    REQUIRE(gate.shouldSend(p));

    CHECK_FALSE(gate.shouldSend(p));          // nothing moved
    CHECK(gate.shouldSend(p, /*force*/ true)); // ...but the console said so
}

TEST_CASE("Adopting a baseline makes the gate believe the engine holds it",
          "[core][reverb][gate]")
{
    core::ReverbSendGate gate;
    REQUIRE(gate.shouldSend(hall()));

    core::ReverbParams other = hall();
    other.decaySec = 5.2f;   // a Cathedral: far past the epsilon
    gate.adoptBaseline(other);

    CHECK(gate.lastSent().decaySec == Approx(5.2f));
    CHECK_FALSE(gate.shouldSend(other)); // it believes this already arrived
}

TEST_CASE("Resetting the gate makes it send again", "[core][reverb][gate]")
{
    // prepareToPlay re-prepares the reverb, so nothing about its previous state
    // is knowable any more.
    core::ReverbSendGate gate;
    const core::ReverbParams p = hall();
    REQUIRE(gate.shouldSend(p));
    REQUIRE_FALSE(gate.shouldSend(p));

    gate.reset();
    CHECK_FALSE(gate.primed());
    CHECK(gate.shouldSend(p));
}

TEST_CASE("The reverb gate compares at the rate the reverb will use",
          "[core][reverb][gate][regression]")
{
    // The damping corner is clamped against Nyquist, so a request above it
    // resolves to different values at different sample rates. A gate holding the
    // wrong rate compares a raw request against a differently-clamped stored value
    // and reports a change forever -- one message per block, which is the exact
    // traffic it was built to stop.
    core::ReverbSendGate gate;
    gate.setSampleRate(48000.0); // ceiling 23520 Hz

    core::ReverbParams p = hall();
    p.dampingHz = 30000.0f;      // above it
    REQUIRE(gate.shouldSend(p));

    // The same impossible request is not a new one.
    for (int i = 0; i < 50; ++i)
        CHECK_FALSE(gate.shouldSend(p));

    // And at a rate where 30 kHz IS reachable, a move up to it is a real change.
    core::ReverbSendGate fast;
    fast.setSampleRate(96000.0); // ceiling 47040 Hz
    core::ReverbParams q = hall();
    q.dampingHz = 20000.0f;
    REQUIRE(fast.shouldSend(q));
    q.dampingHz = 30000.0f;
    CHECK(fast.shouldSend(q));
}
