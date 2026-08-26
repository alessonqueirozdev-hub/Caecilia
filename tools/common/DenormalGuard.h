// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
 #include <pmmintrin.h>
 #include <xmmintrin.h>
 #define CAECILIA_TOOLS_HAVE_FTZ 1
#else
 #define CAECILIA_TOOLS_HAVE_FTZ 0
#endif

namespace caecilia::tools
{

/**
 * @brief Put this thread into flush-to-zero / denormals-are-zero for its lifetime.
 *
 * The plugin runs its whole audio callback inside juce::ScopedNoDenormals, so FTZ
 * and DAZ are ON for every sample a listener ever hears. An offline harness that
 * measures WITHOUT them is not measuring the instrument: a decaying release tail
 * runs its partial gains down through the denormal range, and on x86 every
 * denormal operation traps to microcode. Measured consequence in this project: a
 * single sustained voice appeared to cost 144% of a core, and repeat runs of the
 * same unmodified binary disagreed by 1.5x.
 *
 * So any tool that reports a CPU figure, or that renders audio meant to match
 * what the plugin produces, must set this first. It is a two-instruction thread
 * mode, restored on destruction.
 */
class DenormalGuard
{
public:
    DenormalGuard() noexcept
    {
#if CAECILIA_TOOLS_HAVE_FTZ
        previousFlush_    = _MM_GET_FLUSH_ZERO_MODE();
        previousDenormals_ = _MM_GET_DENORMALS_ZERO_MODE();
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    }

    ~DenormalGuard() noexcept
    {
#if CAECILIA_TOOLS_HAVE_FTZ
        _MM_SET_FLUSH_ZERO_MODE(previousFlush_);
        _MM_SET_DENORMALS_ZERO_MODE(previousDenormals_);
#endif
    }

    DenormalGuard(const DenormalGuard&)            = delete;
    DenormalGuard& operator=(const DenormalGuard&) = delete;

    /// @return true if this build can actually set the mode.
    [[nodiscard]] static constexpr bool available() noexcept
    {
        return CAECILIA_TOOLS_HAVE_FTZ != 0;
    }

private:
#if CAECILIA_TOOLS_HAVE_FTZ
    unsigned int previousFlush_     = 0;
    unsigned int previousDenormals_ = 0;
#endif
};

} // namespace caecilia::tools
