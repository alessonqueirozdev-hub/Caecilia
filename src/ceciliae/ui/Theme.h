/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

#include <array>
#include <cstdint>
#include <string>

/**
 * @file Theme.h
 * @brief The JUCE-free design-token model that both console skins and the
 *        @c CeciliaeLookAndFeel read.
 *
 * A @c Theme is a flat bag of semantic colour/metric TOKENS, not a set of
 * hard-coded widget colours. Keeping it JUCE-free means the token set is shared
 * verbatim between the photoreal and flat skins, is hot-swappable at runtime,
 * and can be unit-tested. The tonal-family tint table is a first-class token so
 * a drawstop can always be tinted by what it *is* (a red reed, a blue flute),
 * consistently across both skins and light/dark variants.
 */

namespace ceciliae::ui
{

/// Straight (non-premultiplied) 8-bit-per-channel colour; converted to
/// @c juce::Colour by the skins at paint time. Kept JUCE-free on purpose.
struct Rgba
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    friend constexpr bool operator==(Rgba, Rgba) noexcept = default;
};

/// Overall brightness variant a theme expresses.
enum class ThemeMode : std::uint8_t
{
    Light,
    Dark
};

/// The rendering skin a theme is authored for (both share the same tokens).
enum class SkinId : std::uint8_t
{
    Photoreal, ///< Wood, tactile drawstops, procedural shading.
    Flat       ///< Modern minimal flat-vector look.
};

/**
 * @brief The complete token set for one theme.
 *
 * Colours are semantic ("surface", "accent", "keyLitDirect") rather than
 * component-specific, so a new skin never invents its own palette. Metrics are
 * expressed in logical canvas units (see @c UiGeometry.h).
 */
struct ThemeTokens
{
    // --- Identity -----------------------------------------------------------
    std::string name = "Ceciliae Default";
    ThemeMode   mode = ThemeMode::Dark;

    // --- Surfaces & text ----------------------------------------------------
    Rgba surface       { 24, 24, 28, 255 };  ///< Console backdrop.
    Rgba surfaceRaised { 38, 38, 44, 255 };  ///< Jambs, panels, rails.
    Rgba surfaceSunken { 14, 14, 16, 255 };  ///< Recesses, key beds.
    Rgba textPrimary   { 236, 236, 240, 255 };
    Rgba textSecondary { 160, 160, 168, 255 };
    Rgba outline       { 8, 8, 10, 255 };    ///< Hairlines and edges.

    // --- Accents ------------------------------------------------------------
    Rgba accent        { 196, 148, 74, 255 };  ///< Warm brass accent.
    Rgba accentActive  { 236, 190, 110, 255 };

    // --- Live key illumination (blue / red / purple, per real consoles) -----
    Rgba keyLitDirect      { 66, 148, 236, 255 };  ///< Blue: key played on this manual.
    Rgba keyLitCoupled     { 224, 78, 78, 255 };   ///< Red: sounded via a coupler.
    Rgba keyLitCombination { 168, 96, 216, 255 };  ///< Purple: driven by a combination/sequencer.

    // --- Meters -------------------------------------------------------------
    Rgba meterLow   { 96, 200, 120, 255 };
    Rgba meterMid   { 224, 200, 96, 255 };
    Rgba meterHigh  { 224, 96, 96, 255 };
    Rgba windGauge  { 120, 176, 224, 255 };

    // --- Tonal-family tint table (first-class token) ------------------------
    /// Indexed by @c static_cast<std::size_t>(core::TonalFamily). Size covers
    /// every enumerator including @c Undefined.
    std::array<Rgba, 8> familyTint {{
        Rgba{ 116, 156, 224, 255 }, ///< Principal
        Rgba{  96, 200, 168, 255 }, ///< Flute
        Rgba{ 200, 168,  96, 255 }, ///< String
        Rgba{ 224,  96,  96, 255 }, ///< Reed
        Rgba{ 176, 116, 224, 255 }, ///< Mixture
        Rgba{ 160, 160, 168, 255 }, ///< Hybrid
        Rgba{ 216, 176, 128, 255 }, ///< Percussion
        Rgba{ 128, 128, 136, 255 }  ///< Undefined
    }};

    // --- Metrics (logical units) -------------------------------------------
    float cornerRadius = 4.0f;
    float hairline     = 1.0f;
    float drawstopSize = 56.0f;

    /// @return the tint for a tonal family (falls back to @c Undefined).
    [[nodiscard]] Rgba tintFor(core::TonalFamily family) const noexcept
    {
        const auto i = static_cast<std::size_t>(family);
        return i < familyTint.size() ? familyTint[i] : familyTint.back();
    }
};

/**
 * @brief A resolved theme: the tokens plus the skin they were authored for.
 *
 * Value-semantic and cheap to copy. The @c ThemeManager owns the active theme
 * and hands const references to the skins and the look-and-feel.
 */
class Theme
{
public:
    Theme() = default;
    Theme(std::string id, SkinId skin, ThemeTokens tokens)
        : id_(std::move(id)), skin_(skin), tokens_(std::move(tokens))
    {
    }

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] SkinId              skin() const noexcept { return skin_; }
    [[nodiscard]] ThemeMode           mode() const noexcept { return tokens_.mode; }
    [[nodiscard]] const ThemeTokens&  tokens() const noexcept { return tokens_; }

    /// Replace the token set in place (used by the manager's hot-swap).
    void setTokens(ThemeTokens tokens) { tokens_ = std::move(tokens); }
    void setSkin(SkinId skin) noexcept { skin_ = skin; }

private:
    std::string id_    = "default";
    SkinId      skin_  = SkinId::Flat;
    ThemeTokens tokens_{};
};

} // namespace ceciliae::ui
