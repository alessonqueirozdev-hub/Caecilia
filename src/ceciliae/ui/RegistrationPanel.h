/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/registration/Selector.h"
#include "ceciliae/registration/SelectorParser.h"
#include "ceciliae/registration/StopSet.h"
#include "ceciliae/ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace ceciliae::model { class Organ; }
namespace ceciliae::registration { struct RegistrationState; }

/**
 * @file RegistrationPanel.h
 * @brief The registration-BY-INTENT surface: a family x footage palette that
 *        drives the queryable registration brain visually.
 *
 * This is the UI expression of Ceciliae's registration moat. Instead of hunting
 * individual drawknobs, the player works in intent: pick a tonal family and a
 * footage and apply a set operation (union / difference / intersection / solo),
 * or ask for a domain-intelligent build-plenum, all with undo. Every action is
 * built through the SAME @c SelectorParser grammar the OSC/JSON-RPC/MIDI-learn
 * surfaces use, so a click and a script mean exactly the same thing.
 *
 * Selecting a cell previews the matching stop set (so the console can highlight
 * the drawstops that would change) before it is applied. The panel owns no
 * registration state — it resolves lazily against the supplied @c OrganSpec and
 * live state and emits intents through callbacks. Aliased as
 * @c RegistrationPaletteComponent in design vocabulary.
 */

namespace ceciliae::ui
{

/// The set operation an applied intent performs against the current draw.
enum class PaletteOp
{
    Set,          ///< Replace the draw with the selection.
    Union,        ///< Add the selection (a | b).
    Difference,   ///< Remove the selection (a - b).
    Intersection, ///< Keep only the selection (a & b).
    Solo          ///< Draw only the selection (mute everything else).
};

class RegistrationPanel final : public juce::Component
{
public:
    RegistrationPanel();

    /// Bind the loaded organ (not owned) used to resolve previews.
    void setOrgan(const model::Organ* organ) noexcept { organ_ = organ; repaint(); }

    /// Bind the live registration state (not owned) for `engaged`/complement
    /// resolution. May be nullptr (an empty state is assumed).
    void setCurrentState(const registration::RegistrationState* state) noexcept { state_ = state; }

    void setThemeTokens(const ThemeTokens& tokens) { tokens_ = tokens; repaint(); }

    // --- Intent callbacks ---------------------------------------------------
    /// Apply an intent built from the palette (op + resolved selector).
    std::function<void(PaletteOp, const registration::Selector&)> onApplyIntent;
    /// Request the domain-intelligent principal plenum (16-8-4-2 + mixtures).
    std::function<void()> onBuildPlenum;
    /// Clear the whole registration.
    std::function<void()> onClear;
    /// Undo the last registration command (branching history lives in the brain).
    std::function<void()> onUndo;
    /// Live preview of the stops a hovered/selected cell would match.
    std::function<void(const registration::StopSet&)> onSelectionPreview;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

private:
    struct FamilyRow { core::TonalFamily family; const char* label; };
    struct FootageCol { core::Footage footage; };

    /// Build the selector for a (family, footage) cell via the shared grammar.
    [[nodiscard]] registration::Selector selectorForCell(int row, int col) const;
    /// Resolve and emit the preview for a cell (message thread).
    void previewCell(int row, int col);
    /// Map a point to a grid cell, or (-1,-1).
    void cellAt(juce::Point<int> p, int& row, int& col) const noexcept;

    const model::Organ*                       organ_ = nullptr;
    const registration::RegistrationState*    state_ = nullptr;
    registration::SelectorParser              parser_;
    ThemeTokens                               tokens_{};

    std::vector<FamilyRow>  families_;
    std::vector<FootageCol> footages_;
    PaletteOp               op_ = PaletteOp::Union;

    int hoverRow_ = -1;
    int hoverCol_ = -1;

    juce::Rectangle<int> opBar_;
    juce::Rectangle<int> grid_;
    juce::Rectangle<int> actionBar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RegistrationPanel)
};

/// Design-vocabulary alias.
using RegistrationPaletteComponent = RegistrationPanel;

} // namespace ceciliae::ui
