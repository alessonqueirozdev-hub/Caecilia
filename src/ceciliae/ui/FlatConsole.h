/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/ui/ISkin.h"
#include "ceciliae/ui/Theme.h"

/**
 * @file FlatConsole.h
 * @brief The modern, minimal FLAT console mode — a clean flat-vector @c ISkin.
 *
 * One of the two runtime-selectable console modes. It renders the same shared
 * layout model with flat fills, hairline outlines and the tonal-family tint
 * tokens, favouring clarity and legibility over realism. Being pure vector it is
 * crisp at any zoom. Aliased as @c FlatVectorSkin in design vocabulary.
 */

namespace ceciliae::ui
{

class FlatConsole final : public ISkin
{
public:
    FlatConsole() = default;
    ~FlatConsole() override = default;

    [[nodiscard]] std::string_view name() const noexcept override { return "Flat"; }
    [[nodiscard]] SkinId           id() const noexcept override { return SkinId::Flat; }

    void setTheme(const Theme& theme) override { tokens_ = theme.tokens(); }

    void paintBackground(juce::Graphics&, const LayoutRect&) override;
    void paintJamb(juce::Graphics&, const ConsoleElement&) override;
    void paintLabel(juce::Graphics&, const ConsoleElement&) override;
    void paintDrawstop(juce::Graphics&, const ConsoleElement&, const DrawstopVisual&) override;
    void paintKeyboard(juce::Graphics&, const ConsoleElement&, const KeyStateSnapshot&, std::size_t) override;
    void paintCoupler(juce::Graphics&, const ConsoleElement&, const CouplerVisual&) override;
    void paintMeter(juce::Graphics&, const ConsoleElement&, const core::engine::MeterFrame&) override;
    void paintWindGauge(juce::Graphics&, const ConsoleElement&, float pressurePa, float sagNorm) override;

private:
    ThemeTokens tokens_{};
};

/// Design-vocabulary alias: the flat skin is also known as the FlatVectorSkin.
using FlatVectorSkin = FlatConsole;

} // namespace ceciliae::ui
