/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/model/Coupler.h"
#include "ceciliae/ui/UiGeometry.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ceciliae::model { class Organ; }

/**
 * @file ConsoleLayoutModel.h
 * @brief The data-driven console layout: immutable geometry records, each
 *        carrying its "pillar-5" semantic identity, auto-laid-out from the
 *        loaded @c OrganSpec.
 *
 * This is the FIRST of the UI pillar's three layers (model / state-mirror /
 * skin). Both console modes render the SAME layout model; only the @c ISkin
 * differs. Because the model is JUCE-free it is unit-testable, and because every
 * element carries one semantic identity, that single fact feeds the tooltip, the
 * screen-reader label, the MIDI-learn target and the OSC address without drift.
 */

namespace ceciliae::ui
{

/// What a console element represents. Drives both hit-testing and which skin
/// routine paints it.
enum class ElementRole : std::uint8_t
{
    Manual,          ///< A playable keyboard (one per manual division).
    Pedalboard,      ///< The pedal keyboard.
    Drawstop,        ///< A drawknob / tab controlling one stop.
    Coupler,         ///< A coupler tab on the coupler rail.
    Piston,          ///< A combination piston.
    SequencerButton, ///< Sequencer Previous/Next (Set/Next stepper).
    ExpressionPedal, ///< A swell/expression shoe.
    WindGauge,       ///< A wind-reservoir / pressure gauge.
    VuMeter,         ///< An output level meter.
    Jamb,            ///< Static stop-jamb chrome (no interaction).
    Label,           ///< Static text chrome.
    Unknown
};

/**
 * @brief The single semantic fact behind an element ("Great Principal 8'").
 *
 * Populated off the audio thread during @c layoutFrom from the organ spec, then
 * immutable. One instance is the source for the tooltip, the accessibility
 * label, the MIDI-learn target and the OSC address.
 */
struct SemanticId
{
    ElementRole       role     = ElementRole::Unknown;
    core::StopId      stop{};                              ///< Valid when role == Drawstop.
    core::DivisionId  division{};                          ///< Owning division, where meaningful.
    model::CouplerId  coupler{};                           ///< Valid when role == Coupler.
    core::TonalFamily family   = core::TonalFamily::Undefined;

    std::string label;      ///< Human-readable, e.g. "Great Principal 8'".
    std::string oscAddress; ///< Stable address, e.g. "/ceciliae/great/principal-8".
};

/**
 * @brief One immutable, positioned element of the console.
 *
 * Geometry is in logical canvas units; @c ConsoleView maps it through the
 * @c ViewportController at paint time. Value-semantic and cheap to copy.
 */
struct ConsoleElement
{
    ElementRole       role   = ElementRole::Unknown;
    LayoutRect        bounds{};
    SemanticId        semantic{};
    core::Footage     footage{};                          ///< For drawstops: the sounding footage to label.
    core::TonalFamily family = core::TonalFamily::Undefined; ///< For tinting via the theme.
    core::MidiNote    lowNote  = 0;                       ///< For Manual/Pedalboard: compass low.
    core::MidiNote    highNote = 0;                       ///< For Manual/Pedalboard: compass high.
    std::uint16_t     index    = 0;                       ///< Index within its role group (piston number, etc.).
    bool              learnable = false;                  ///< True if a MIDI-learn badge applies.
};

/**
 * @brief Auto-lays-out an @c OrganSpec into a flat list of console elements.
 *
 * Everything here runs off the audio thread. The layout is deterministic for a
 * given organ + canvas so re-layout on resize is stable.
 */
class ConsoleLayoutModel
{
public:
    ConsoleLayoutModel() = default;

    /**
     * @brief Build the element list for @p organ within @p canvas.
     *
     * Places one keyboard per manual/pedal division, its drawstops on jambs, the
     * couplers on a rail, the combination bar and meters/pedals along the frame.
     * Replaces any previous layout. Not real-time safe (allocates).
     */
    void layoutFrom(const model::Organ& organ, LayoutRect canvas);

    /// Drop the current layout.
    void clear() noexcept;

    // --- Access -------------------------------------------------------------
    [[nodiscard]] std::span<const ConsoleElement> elements() const noexcept { return elements_; }
    [[nodiscard]] std::size_t size() const noexcept { return elements_.size(); }
    [[nodiscard]] LayoutRect  canvasBounds() const noexcept { return canvas_; }
    [[nodiscard]] const model::Organ* organ() const noexcept { return organ_; }

    /**
     * @brief Topmost element containing @p p in logical space, or nullptr.
     *
     * Iterates back-to-front so later (visually-on-top) elements win. Message
     * thread only.
     */
    [[nodiscard]] const ConsoleElement* hitTest(LayoutPoint p) const noexcept;

private:
    std::vector<ConsoleElement> elements_;
    LayoutRect                  canvas_{};
    const model::Organ*         organ_ = nullptr;
};

/// @return a lower-case, hyphenated slug of @p text suitable for an OSC path
///         segment (spaces/punctuation collapsed to single hyphens). Off-thread.
[[nodiscard]] std::string slugify(std::string_view text);

} // namespace ceciliae::ui
