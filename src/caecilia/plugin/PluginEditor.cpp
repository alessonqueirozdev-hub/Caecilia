/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/plugin/PluginEditor.h"

#include "BinaryData.h"

#include <cstdint>
#include <cstdio>

namespace caecilia::plugin
{

namespace
{
constexpr int kDefaultWidth  = 1440;
constexpr int kDefaultHeight = 900;
constexpr int kRefreshHz     = 30;

using Resource = juce::WebBrowserComponent::Resource;

/// Map a console family label to the engine's tonal family.
core::TonalFamily familyFromString(const juce::String& fam)
{
    if (fam == "Principal") return core::TonalFamily::Principal;
    if (fam == "Flute")     return core::TonalFamily::Flute;
    if (fam == "String")    return core::TonalFamily::String;
    if (fam == "Reed")      return core::TonalFamily::Reed;
    if (fam == "Mixture")   return core::TonalFamily::Mixture;
    if (fam == "Cornet")    return core::TonalFamily::Mixture; // compound crown
    return core::TonalFamily::Principal;
}
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

    // WebView2 requires a WRITABLE user-data folder. %TEMP% can be cleaned or
    // locked mid-session; a dedicated per-user app-data folder is the robust
    // choice and is created up front so WebView2 never fails to initialise.
    auto dataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("Caecilia").getChildFile("WebView2");
    dataDir.createDirectory();

    return Options{}
        // WebView2 (Chromium) on Windows; ignored on Linux/macOS which use their
        // system engine.
        .withBackend(Options::Backend::webview2)
        .withWinWebView2Options(Options::WinWebView2{}
            .withUserDataFolder(dataDir)
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
            })
        // The whole drawn registration (family + footage of each engaged rank);
        // the engine rebuilds its composite voicing from this — the real audio path.
        .withNativeFunction("caeciliaSetRegistration",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                std::vector<model::RegistrationRank> ranks;
                if (! args.isEmpty() && args[0].isArray())
                {
                    for (const juce::var& e : *args[0].getArray())
                    {
                        const juce::String fam = e.getProperty("fam", "Principal").toString();
                        model::RegistrationRank r;
                        r.family   = familyFromString(fam);
                        r.compound = (fam == "Mixture" || fam == "Cornet");
                        r.footage  = model::footageFromFeet(static_cast<double>(e.getProperty("feet", 8.0)));
                        ranks.push_back(r);
                    }
                }
                proc.setUiRegistration(ranks);
                complete(juce::var());
            })
        // --- Settings panel -> engine (master trim, reverb space + mix) -------
        .withNativeFunction("caeciliaSetMaster",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (! args.isEmpty())
                    proc.setUiMaster(static_cast<float>(static_cast<double>(args[0])));
                complete(juce::var());
            })
        .withNativeFunction("caeciliaSetReverb",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                    proc.setUiReverb(static_cast<int>(args[0]), static_cast<float>(static_cast<double>(args[1])));
                complete(juce::var());
            })
        .withNativeFunction("caeciliaPanic",
            [&proc](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                proc.uiAllNotesOff();
                complete(juce::var());
            })
        // --- Sequencer page-turn config: the console sets which MIDI keys step the
        // registration sequencer (configurable per keyboard model) and can arm a
        // MIDI-learn so the user just presses the key they want.
        .withNativeFunction("caeciliaSeqConfig",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 3)
                    proc.setSeqNav(static_cast<int>(args[0]), static_cast<int>(args[1]),
                                   static_cast<bool>(args[2]));
                complete(juce::var());
            })
        .withNativeFunction("caeciliaSeqLearn",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                proc.armSeqLearn(args.isEmpty() ? 0 : static_cast<int>(args[0]));
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

    // Page-turn intents from the swallowed nav keys -> step the console sequencer.
    for (std::int8_t dir; processor_.popSeqNav(dir); )
        web_.emitEventIfBrowserIsVisible("caeciliaSeq", juce::var(static_cast<int>(dir)));

    // A MIDI-learn capture -> tell the console which key was bound so it updates
    // its field (encoded which*256 + note).
    const int learned = processor_.takeLearnedNote();
    if (learned >= 0)
    {
        auto* ev = new juce::DynamicObject();
        ev->setProperty("which", learned / 256);
        ev->setProperty("note",  learned % 256);
        web_.emitEventIfBrowserIsVisible("caeciliaSeqLearned", juce::var(ev));
    }
}

void CaeciliaEditor::resized()
{
    web_.setBounds(getLocalBounds());
}

} // namespace caecilia::plugin
