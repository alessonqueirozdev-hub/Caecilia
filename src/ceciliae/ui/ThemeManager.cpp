/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/ui/ThemeManager.h"

#include <algorithm>
#include <utility>

namespace ceciliae::ui
{

Theme makeDefaultFlatDark()
{
    ThemeTokens t{};
    t.name = "Flat Dark";
    t.mode = ThemeMode::Dark;
    return Theme{ "flat-dark", SkinId::Flat, std::move(t) };
}

Theme makeDefaultFlatLight()
{
    ThemeTokens t{};
    t.name          = "Flat Light";
    t.mode          = ThemeMode::Light;
    t.surface       = Rgba{ 244, 244, 246, 255 };
    t.surfaceRaised = Rgba{ 255, 255, 255, 255 };
    t.surfaceSunken = Rgba{ 226, 226, 230, 255 };
    t.textPrimary   = Rgba{ 24, 24, 28, 255 };
    t.textSecondary = Rgba{ 96, 96, 104, 255 };
    t.outline       = Rgba{ 200, 200, 206, 255 };
    return Theme{ "flat-light", SkinId::Flat, std::move(t) };
}

Theme makeDefaultPhotoreal()
{
    ThemeTokens t{};
    t.name          = "Walnut";
    t.mode          = ThemeMode::Dark;
    t.surface       = Rgba{ 46, 30, 20, 255 };  ///< Deep walnut backdrop.
    t.surfaceRaised = Rgba{ 74, 48, 30, 255 };  ///< Lit jamb face.
    t.surfaceSunken = Rgba{ 28, 18, 12, 255 };
    t.textPrimary   = Rgba{ 236, 224, 204, 255 };
    t.textSecondary = Rgba{ 184, 160, 128, 255 };
    t.accent        = Rgba{ 208, 168, 96, 255 };
    return Theme{ "photoreal-walnut", SkinId::Photoreal, std::move(t) };
}

ThemeManager::ThemeManager()
{
    // Seed the registry with the built-in themes and start on flat dark.
    registerTheme(makeDefaultFlatDark());
    registerTheme(makeDefaultFlatLight());
    registerTheme(makeDefaultPhotoreal());
    active_ = themes_.front().theme;
}

void ThemeManager::registerTheme(Theme theme)
{
    const auto id = theme.id();
    const auto it = std::find_if(themes_.begin(), themes_.end(),
                                 [&](const Entry& e) { return e.id == id; });
    if (it != themes_.end())
        it->theme = std::move(theme);
    else
        themes_.push_back(Entry{ id, std::move(theme) });
}

std::vector<std::string> ThemeManager::themeIds() const
{
    std::vector<std::string> ids;
    ids.reserve(themes_.size());
    for (const auto& e : themes_)
        ids.push_back(e.id);
    return ids;
}

bool ThemeManager::activate(const std::string& id)
{
    const auto it = std::find_if(themes_.begin(), themes_.end(),
                                 [&](const Entry& e) { return e.id == id; });
    if (it == themes_.end())
        return false;

    active_ = it->theme;
    notify();
    return true;
}

void ThemeManager::toggleLightDark()
{
    // Prefer switching to a registered sibling that differs only in mode; if none
    // exists, flip the active tokens in place so the toggle is always meaningful.
    const auto targetMode = active_.mode() == ThemeMode::Dark ? ThemeMode::Light : ThemeMode::Dark;
    for (const auto& e : themes_)
    {
        if (e.theme.skin() == active_.skin() && e.theme.mode() == targetMode)
        {
            active_ = e.theme;
            notify();
            return;
        }
    }

    ThemeTokens t = active_.tokens();
    t.mode = targetMode;
    active_.setTokens(std::move(t)); // TODO(v0.9): derive a proper inverted palette.
    notify();
}

void ThemeManager::setActiveSkin(SkinId skin)
{
    active_.setSkin(skin);
    notify();
}

std::size_t ThemeManager::addListener(Listener listener)
{
    const auto token = nextToken_++;
    // Fire once immediately so the observer initialises from the current theme.
    if (listener)
        listener(active_);
    listeners_.emplace_back(token, std::move(listener));
    return token;
}

void ThemeManager::removeListener(std::size_t token)
{
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [&](const auto& p) { return p.first == token; }),
                     listeners_.end());
}

void ThemeManager::notify() const
{
    for (const auto& [token, listener] : listeners_)
    {
        (void) token;
        if (listener)
            listener(active_);
    }
}

} // namespace ceciliae::ui
