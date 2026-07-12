/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/plugin/PluginEditor.h"

#include "BinaryData.h"

namespace caecilia::plugin
{

namespace
{
constexpr int kDefaultWidth  = 1440;
constexpr int kDefaultHeight = 900;
constexpr int kRefreshHz     = 30;

using Resource = juce::WebBrowserComponent::Resource;
} // namespace

std::optional<Resource> CaeciliaEditor::provide(const juce::String& url)
{
    // The mockup is one self-contained document; serve it at the root / index.html
    // and nothing else (all CSS/JS/images are inlined).
    const bool isRoot = url == "/" || url == "/index.html" || url.isEmpty();
    if (! isRoot)
        return std::nullopt;

    // The mockup file is Artifact-style BODY content (no <html>/<head>/charset);
    // wrap the RAW UTF-8 bytes (no String round-trip) in a proper document with an
    // explicit charset, else WebKit falls back to latin-1 and mangles the accented
    // French stop names (Pédale, Récit) and the ' / — glyphs.
    static constexpr char kPrefix[] =
        "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>Caecilia Console</title></head><body style=\"margin:0;padding:0\">";
    static constexpr char kSuffix[] = "</body></html>";

    // A leading UTF-8 BOM is the strongest possible encoding signal and forces
    // UTF-8 decoding even when the WebKitGTK resource-provider path drops the
    // Content-Type charset (which it does) and the <meta charset> is missed.
    static constexpr unsigned char kBom[] = { 0xEF, 0xBB, 0xBF };

    const auto* bom = reinterpret_cast<const std::byte*>(kBom);
    const auto* pre = reinterpret_cast<const std::byte*>(kPrefix);
    const auto* mid = reinterpret_cast<const std::byte*>(BinaryData::console_html);
    const auto* suf = reinterpret_cast<const std::byte*>(kSuffix);
    const auto  preLen = sizeof(kPrefix) - 1;
    const auto  midLen = static_cast<size_t>(BinaryData::console_htmlSize);
    const auto  sufLen = sizeof(kSuffix) - 1;

    std::vector<std::byte> data;
    data.reserve(sizeof(kBom) + preLen + midLen + sufLen);
    data.insert(data.end(), bom, bom + sizeof(kBom));
    data.insert(data.end(), pre, pre + preLen);
    data.insert(data.end(), mid, mid + midLen);
    data.insert(data.end(), suf, suf + sufLen);
    return Resource{ std::move(data), juce::String("text/html; charset=utf-8") };
}

juce::WebBrowserComponent::Options CaeciliaEditor::makeOptions()
{
    using Options = juce::WebBrowserComponent::Options;

    auto& proc = processor_;

    return Options{}
        // WebView2 (Chromium) on Windows; ignored on Linux/macOS which use their
        // system engine. A writable user-data folder is required by WebView2.
        .withBackend(Options::Backend::webview2)
        .withWinWebView2Options(Options::WinWebView2{}
            .withUserDataFolder(juce::File::getSpecialLocation(juce::File::tempDirectory))
            .withBackgroundColour(juce::Colour(0xff0e1319)))
        .withNativeIntegrationEnabled()
        .withResourceProvider([](const juce::String& url) { return provide(url); })
        // --- JS -> C++ : the mockup's drawstops and keys become engine commands.
        .withNativeFunction("caeciliaToggleStop",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (! args.isEmpty())
                    proc.toggleStop(core::StopId{ static_cast<std::uint16_t>(static_cast<int>(args[0])) });
                complete(juce::var());
            })
        .withNativeFunction("caeciliaNote",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 3)
                {
                    const auto div  = static_cast<std::uint16_t>(static_cast<int>(args[0]));
                    const auto note = static_cast<core::MidiNote>(static_cast<int>(args[1]));
                    const bool down = static_cast<bool>(args[2]);
                    proc.uiNote(core::DivisionId{ div }, note, down);
                }
                complete(juce::var());
            });
}

CaeciliaEditor::CaeciliaEditor(CaeciliaAudioProcessor& processor)
    : juce::AudioProcessorEditor(&processor)
    , processor_(processor)
    , web_(makeOptions())
{
    addAndMakeVisible(web_);
    web_.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable(true, true);
    setResizeLimits(960, 600, 4096, 2560);
    setSize(kDefaultWidth, kDefaultHeight);

    startTimerHz(kRefreshHz);
}

CaeciliaEditor::~CaeciliaEditor()
{
    stopTimer();
}

void CaeciliaEditor::timerCallback()
{
    // Push one consistent frame (lit keys + meters) to the page. The mockup listens
    // for "caeciliaState" and updates its key lights, VU and wind gauges.
    const ui::ConsoleFrame frame = processor_.stateMirror().read();

    auto* obj = new juce::DynamicObject();
    obj->setProperty("voices",   static_cast<int>(frame.meters.activeVoices));
    obj->setProperty("masterPeak", frame.meters.master.peak);
    obj->setProperty("windSag",  frame.meters.windSagNorm);
    obj->setProperty("playDiv",  static_cast<int>(processor_.playDivision().value));

    // Lit keys of the primary manual, as a compact [note,...] array (source != Off).
    juce::Array<juce::var> lit;
    const auto div = processor_.playDivision().value;
    for (int n = 0; n < 128; ++n)
        if (frame.keys.get(div, static_cast<core::MidiNote>(n)) != ui::KeySource::Off)
            lit.add(n);
    obj->setProperty("litDiv", static_cast<int>(div));
    obj->setProperty("lit", lit);

    web_.emitEventIfBrowserIsVisible("caeciliaState", juce::var(obj));
}

void CaeciliaEditor::resized()
{
    web_.setBounds(getLocalBounds());
}

} // namespace caecilia::plugin
