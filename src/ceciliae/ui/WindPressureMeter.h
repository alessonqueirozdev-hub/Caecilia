/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * @file WindPressureMeter.h
 * @brief An animated reservoir / wind-pressure gauge that visibly SAGS under
 *        polyphony load.
 *
 * This is the visible face of Ceciliae's realism moat: the wind model is an
 * audio-rate control system, and this gauge makes that legible. It shows the
 * current chest pressure against nominal and animates a bellows plate that
 * droops as sag increases and lifts as the reservoir recovers, with the
 * tremulant wobble overlaid. Values are pushed by the host from the polled
 * @c MeterSnapshot; the gauge smooths them internally for a physical feel.
 */

namespace ceciliae::ui
{

class WindPressureMeter final : public juce::Component,
                                private juce::Timer
{
public:
    WindPressureMeter();
    ~WindPressureMeter() override;

    /**
     * @brief Push the latest wind state.
     * @param pressurePa     Current representative chest pressure (Pa).
     * @param nominalPa      The nominal (target) pressure for reference.
     * @param sagNorm        Normalised sag (0 = nominal, negative = drooping).
     * @param tremulantPhase Tremulant phase in [0,1) for the overlaid wobble.
     */
    void setWindState(float pressurePa, float nominalPa, float sagNorm, float tremulantPhase);

    void setThemeTokens(const ThemeTokens& tokens) { tokens_ = tokens; repaint(); }

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    ThemeTokens tokens_{};
    float pressurePa_     = 800.0f;
    float nominalPa_      = 800.0f;
    float sagNorm_        = 0.0f;
    float tremulantPhase_ = 0.0f;

    float displayedFill_  = 1.0f; ///< Smoothed 0..1 reservoir fill for animation.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WindPressureMeter)
};

} // namespace ceciliae::ui
