/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/model/Coupler.h"
#include "ceciliae/ui/ConsoleLayoutModel.h"
#include "ceciliae/ui/FlatConsole.h"
#include "ceciliae/ui/PhotorealConsole.h"
#include "ceciliae/ui/StateMirror.h"
#include "ceciliae/ui/Theme.h"
#include "ceciliae/ui/ViewportController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

/**
 * @file ConsoleView.h
 * @brief The single console Component: one canvas painted by a pluggable skin,
 *        with NO per-key sub-components.
 *
 * @c ConsoleView is where the three UI layers meet: it reads the immutable
 * @c ConsoleLayoutModel, polls the lock-free @c StateMirror once per display
 * refresh, and paints everything through the active @c ISkin. It hit-tests in
 * logical space through the @c ViewportController (so zoom/pan and clicks agree),
 * repaints on a @c juce::VBlankAttachment with dirty-region invalidation, and
 * exposes semantic callbacks the host wires to the registration engine.
 *
 * Switching console modes (photoreal <-> flat) is a skin pointer swap and a
 * repaint — the model, the live feed and every interaction stay identical.
 */

namespace ceciliae::ui
{

class ConsoleView final : public juce::Component
{
public:
    ConsoleView();
    ~ConsoleView() override;

    // --- Wiring (set by the plugin editor) ----------------------------------
    /// Point at the immutable layout (not owned). Triggers a re-fit and repaint.
    void setLayoutModel(const ConsoleLayoutModel* model);

    /// Point at the live audio->UI mirror (not owned). Polled each refresh.
    void setStateMirror(const StateMirror* mirror) noexcept { mirror_ = mirror; }

    /// Adopt @p theme for the active skin and the console chrome.
    void applyTheme(const Theme& theme);

    /// Select which console mode is active. Repaints; keeps all state.
    void setConsoleMode(SkinId mode);
    [[nodiscard]] SkinId consoleMode() const noexcept { return activeMode_; }

    // --- Viewport -----------------------------------------------------------
    [[nodiscard]] ViewportController& viewport() noexcept { return viewport_; }
    void resetView();

    // --- Semantic interaction callbacks -------------------------------------
    std::function<void(core::StopId)>                          onDrawstopClicked;
    std::function<void(model::CouplerId)>                      onCouplerClicked;
    std::function<void(std::uint16_t)>                         onPistonClicked;
    std::function<void(int /*+1 next, -1 previous*/)>          onSequencerStep;
    std::function<void(core::DivisionId, core::MidiNote, bool /*down*/)> onKeyEvent;

    // --- Live-state queries the host supplies (registration truth) ----------
    std::function<bool(core::StopId)>     stopEngagedQuery;
    std::function<bool(model::CouplerId)> couplerEngagedQuery;

    // --- juce::Component ----------------------------------------------------
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

private:
    /// Called on every display v-blank: poll the mirror and invalidate dirt.
    void onVBlank();

    /// Dispatch a mouse position (logical) to the element under it.
    void handleElementClick(const ConsoleElement& element, bool isDown);

    /// Resolve the active skin pointer.
    [[nodiscard]] ISkin& skin() noexcept;

    /// Convert the viewport transform into a JUCE affine transform.
    [[nodiscard]] juce::AffineTransform sceneTransform() const noexcept;

    const ConsoleLayoutModel* model_  = nullptr;
    const StateMirror*        mirror_ = nullptr;

    FlatConsole      flatSkin_;
    PhotorealConsole photoSkin_;
    SkinId           activeMode_ = SkinId::Flat;

    ViewportController viewport_;

    ConsoleFrame            lastFrame_{};       ///< Most recent polled state.
    const ConsoleElement*   hovered_ = nullptr; ///< Element under the pointer.
    LayoutPoint             lastDragScreen_{};  ///< For pan deltas.
    bool                    panning_ = false;

    // Virtual-keyboard press tracking (mouse-played notes).
    bool             keyDown_ = false;
    core::DivisionId activeKeyDivision_{};
    core::MidiNote   activeKeyNote_ = 0;

    std::unique_ptr<juce::VBlankAttachment> vblank_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConsoleView)
};

} // namespace ceciliae::ui
