// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/plugin/PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>

namespace caecilia::plugin
{

/**
 * @brief The plugin editor — the Caecilia console, rendered as the web mockup.
 *
 * The first-class UI is the hand-tuned HTML/CSS console (docs/mockups/console.html),
 * compiled into the binary and served to an embedded @c juce::WebBrowserComponent
 * (Chromium/WebView2 on Windows, WebKitGTK on Linux, WKWebView on macOS). A
 * JS<->C++ native-integration bridge turns the mockup's drawstops and keys into
 * real engine commands and streams the engine's meters + lit keys back, so the
 * beautiful mockup drives the actual DSP. The audio thread never touches the view.
 */
class CaeciliaEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit CaeciliaEditor(CaeciliaAudioProcessor& processor);
    ~CaeciliaEditor() override;

    void resized() override;
    /// When embedded in the Standalone window, enable resize + add the maximise
    /// title-bar button (JUCE's default standalone window omits it).
    void parentHierarchyChanged() override;

private:
    /// Publish the compiled instrument to the console: divisions, and every stop
    /// with its REAL StopId. Until this existed the page carried its own
    /// hard-coded stop list, eight entries of which had no counterpart in the
    /// organ at all -- drawing one did nothing, silently.
    void pushOrganSpec();

    /// Publish the drawn registration as a list of StopIds.
    void pushRegistration();

    void timerCallback() override; ///< Frame-rate push of meters + lit keys to the page.

    /// Serve the embedded console HTML to the WebView's resource provider.
    static std::optional<juce::WebBrowserComponent::Resource> provide(const juce::String& url);

    /// Build the configured WebBrowserComponent options (native bridge + provider).
    juce::WebBrowserComponent::Options makeOptions();

    CaeciliaAudioProcessor& processor_;
    juce::WebBrowserComponent web_;

    /// Language the installer recorded (HKCU\Software\Caecilia\Language, Windows),
    /// forwarded to the console so it opens in the language chosen at install time.
    juce::String installerLang_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CaeciliaEditor)
};

} // namespace caecilia::plugin
