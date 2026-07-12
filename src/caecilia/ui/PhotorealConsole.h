/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/ui/ISkin.h"
#include "caecilia/ui/Theme.h"

#include <juce_graphics/juce_graphics.h>

/**
 * @file PhotorealConsole.h
 * @brief The tactile, wood-and-drawknobs PHOTOREAL console mode — a vector
 *        @c ISkin using procedural shading and tiled texture brushes.
 *
 * One of the two runtime-selectable console modes. It renders the SAME shared
 * layout model as @c FlatConsole, but with the look of a real instrument: walnut
 * jambs painted with a tiled wood-grain @c juce::TiledImageBrush, turned
 * drawknobs with radial highlights, ivory/ebony keys and engraved stop labels.
 * All realism is vector + brush based so it stays crisp at 4K. Aliased as
 * @c PhotorealSkin in design vocabulary.
 */

namespace caecilia::ui
{

class PhotorealConsole final : public ISkin
{
public:
    PhotorealConsole() = default;
    ~PhotorealConsole() override = default;

    [[nodiscard]] std::string_view name() const noexcept override { return "Photoreal"; }
    [[nodiscard]] SkinId           id() const noexcept override { return SkinId::Photoreal; }

    void setTheme(const Theme& theme) override;

    void paintBackground(juce::Graphics&, const LayoutRect&) override;
    void paintJamb(juce::Graphics&, const ConsoleElement&) override;
    void paintLabel(juce::Graphics&, const ConsoleElement&) override;
    void paintDrawstop(juce::Graphics&, const ConsoleElement&, const DrawstopVisual&) override;
    void paintKeyboard(juce::Graphics&, const ConsoleElement&, const KeyStateSnapshot&, std::size_t) override;
    void paintCoupler(juce::Graphics&, const ConsoleElement&, const CouplerVisual&) override;
    void paintMeter(juce::Graphics&, const ConsoleElement&, const core::engine::MeterFrame&) override;
    void paintWindGauge(juce::Graphics&, const ConsoleElement&, float pressurePa, float sagNorm) override;

private:
    /// Rebuild the cached wood-grain brush from the current tokens.
    void rebuildWoodBrush();

    ThemeTokens tokens_{};
    juce::Image woodTexture_; ///< Procedurally-generated grain tile (cached). // TODO(v0.9)
};

/// Design-vocabulary alias: the photoreal skin is also the PhotorealSkin.
using PhotorealSkin = PhotorealConsole;

} // namespace caecilia::ui
