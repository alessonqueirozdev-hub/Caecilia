/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/ui/Theme.h"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file ThemeManager.h
 * @brief Token-based, hot-swappable theme registry driving both skins and the
 *        look-and-feel from one source of truth.
 *
 * The manager owns a small set of named themes (light/dark variants of skins),
 * exposes the currently active one, and fans a change notification out to every
 * registered observer (the @c ConsoleView, the @c CaeciliaLookAndFeel, any open
 * panel). Because a @c Theme is a flat token bag, switching skins or toggling
 * light/dark is a pointer swap plus a repaint — no component teardown.
 *
 * This type is JUCE-free and lives entirely on the message thread; it performs
 * no audio-thread work.
 */

namespace caecilia::ui
{

class ThemeManager
{
public:
    /// Notified whenever the active theme changes; receives the new theme.
    using Listener = std::function<void(const Theme&)>;

    ThemeManager();

    // --- Registry -----------------------------------------------------------
    /// Register (or replace by id) a theme the user can switch to.
    void registerTheme(Theme theme);

    /// @return the ids of every registered theme, in insertion order.
    [[nodiscard]] std::vector<std::string> themeIds() const;

    // --- Active theme -------------------------------------------------------
    [[nodiscard]] const Theme& active() const noexcept { return active_; }

    /**
     * @brief Switch to the theme with @p id.
     * @return true if a theme with that id existed and was activated.
     *
     * Notifies all listeners on success. No-op (returns false) for an unknown id.
     */
    bool activate(const std::string& id);

    /// Toggle the active theme between its light and dark sibling if one exists,
    /// otherwise flip the active tokens' @c ThemeMode in place. Notifies listeners.
    void toggleLightDark();

    /// Switch only the rendering skin of the active theme (keeps tokens). Notifies.
    void setActiveSkin(SkinId skin);

    // --- Observers ----------------------------------------------------------
    /// Subscribe to change notifications; the listener is called immediately once
    /// with the current theme so it can initialise. @return an opaque token id.
    std::size_t addListener(Listener listener);

    /// Remove a previously-added listener by its token id.
    void removeListener(std::size_t token);

private:
    void notify() const;

    struct Entry { std::string id; Theme theme; };

    std::vector<Entry>                          themes_;
    Theme                                       active_{};
    std::vector<std::pair<std::size_t, Listener>> listeners_;
    std::size_t                                 nextToken_ = 1;
};

/// @return the built-in default dark flat theme.
[[nodiscard]] Theme makeDefaultFlatDark();
/// @return the built-in default light flat theme.
[[nodiscard]] Theme makeDefaultFlatLight();
/// @return the built-in default photoreal (wood) theme.
[[nodiscard]] Theme makeDefaultPhotoreal();

} // namespace caecilia::ui
