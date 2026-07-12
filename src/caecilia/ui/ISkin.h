/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/engine/MeterSnapshot.h"
#include "caecilia/ui/ConsoleLayoutModel.h"
#include "caecilia/ui/StateMirror.h"
#include "caecilia/ui/Theme.h"

#include <string_view>

namespace juce { class Graphics; }

/**
 * @file ISkin.h
 * @brief The THIRD layer of the UI pillar: a pluggable paint strategy that turns
 *        the (shared) layout model + live state mirror into pixels.
 *
 * A skin knows only HOW to draw an element; it owns no state about WHICH
 * elements exist (that is the layout model) or their live values (that is the
 * state mirror). Both console modes — @c PhotorealConsole and @c FlatConsole —
 * are implementations of this one interface, so live feedback, hit-testing,
 * MIDI-learn, zoom/pan, theming and accessibility are all built exactly once.
 *
 * All drawing runs on the message thread inside @c ConsoleView::paint. Skins are
 * expected to be crisp at any zoom (pure vector geometry / texture brushes), so
 * every routine paints into the caller-supplied logical-space @c juce::Graphics
 * (already transformed by the @c ViewportController).
 */

namespace caecilia::ui
{

/// Transient, animated state of a drawstop for one paint pass.
struct DrawstopVisual
{
    bool  engaged   = false; ///< Whether the stop is currently drawn.
    bool  hovered   = false; ///< Pointer is over it.
    float pullNorm  = 0.0f;  ///< Animated 0..1 pull/press progress for smoothing.
};

/// Transient state of a coupler tab for one paint pass.
struct CouplerVisual
{
    bool engaged = false;
    bool hovered = false;
};

/**
 * @brief The paint-strategy interface both console modes implement.
 *
 * Named routines (rather than one generic paintElement) keep each skin's drawing
 * code cohesive and let the photoreal skin cache expensive per-role brushes.
 */
class ISkin
{
public:
    virtual ~ISkin() = default;

    /// @return the human-readable skin name (e.g. "Photoreal", "Flat").
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// @return the skin identity used by the theme system.
    [[nodiscard]] virtual SkinId id() const noexcept = 0;

    /// Adopt @p theme's tokens; rebuild any cached brushes/gradients. Off-thread.
    virtual void setTheme(const Theme& theme) = 0;

    // --- Static chrome (cacheable into a background layer) -------------------
    virtual void paintBackground(juce::Graphics& g, const LayoutRect& canvas) = 0;
    virtual void paintJamb(juce::Graphics& g, const ConsoleElement& element) = 0;
    virtual void paintLabel(juce::Graphics& g, const ConsoleElement& element) = 0;

    // --- Dynamic, live-feedback elements ------------------------------------
    virtual void paintDrawstop(juce::Graphics& g,
                               const ConsoleElement& element,
                               const DrawstopVisual& visual) = 0;

    virtual void paintKeyboard(juce::Graphics& g,
                               const ConsoleElement&   element,
                               const KeyStateSnapshot& keys,
                               std::size_t             divisionSlot) = 0;

    virtual void paintCoupler(juce::Graphics& g,
                              const ConsoleElement& element,
                              const CouplerVisual&  visual) = 0;

    virtual void paintMeter(juce::Graphics& g,
                            const ConsoleElement&             element,
                            const core::engine::MeterFrame&   frame) = 0;

    virtual void paintWindGauge(juce::Graphics& g,
                                const ConsoleElement& element,
                                float pressurePa,
                                float sagNorm) = 0;
};

} // namespace caecilia::ui
