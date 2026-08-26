// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/plugin/PluginEditor.h"

#include "BinaryData.h"

#include "caecilia/model/Division.h"
#include "caecilia/model/Stop.h"

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
        // The page telling us it has finished parsing. Emitting the organ spec
        // before that point drops it: emitEventIfBrowserIsVisible has no queue,
        // and a listener that does not exist yet never hears anything.
        .withNativeFunction("caeciliaReady",
                            [this](const juce::Array<juce::var>&,
                                   juce::WebBrowserComponent::NativeFunctionCompletion c)
                            {
                                pushOrganSpec();
                                pushRegistration();
                                c(juce::var());
                            })

        // A whole registration as StopIds. Replaces the family+footage rank list
        // the console used to send and the processor used to guess its way back
        // from -- the console knows the ids now, because we told it.
        // The swell shoe. It has always moved on screen and done nothing.
        .withNativeFunction("caeciliaSetExpression",
                            [this](const juce::Array<juce::var>& a,
                                   juce::WebBrowserComponent::NativeFunctionCompletion c)
                            {
                                if (a.size() >= 2)
                                    processor_.setUiExpression(static_cast<int>(a[0]),
                                                               static_cast<float>(a[1]));
                                c(juce::var());
                            })

        .withNativeFunction("caeciliaSetDrawnStops",
                            [this](const juce::Array<juce::var>& a,
                                   juce::WebBrowserComponent::NativeFunctionCompletion c)
                            {
                                std::uint64_t bits = 0;
                                if (a.size() >= 1 && a[0].isArray())
                                    for (const juce::var& v : *a[0].getArray())
                                    {
                                        const int id = static_cast<int>(v);
                                        if (id >= 0 && id < 64)
                                            bits |= (std::uint64_t{1} << id);
                                    }
                                processor_.setDrawnStops(bits);
                                c(juce::var());
                            })

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
        .withNativeFunction("caeciliaSetVolume",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (! args.isEmpty())
                    proc.setUiVolume(static_cast<float>(static_cast<double>(args[0])));
                complete(juce::var());
            })
        .withNativeFunction("caeciliaSetReverb",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                    proc.setUiReverb(static_cast<int>(args[0]), static_cast<float>(static_cast<double>(args[1])));
                complete(juce::var());
            })
        // --- Couplers -----------------------------------------------------------
        .withNativeFunction("caeciliaToggleCoupler",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (! args.isEmpty())
                    proc.toggleCoupler(static_cast<std::size_t>(static_cast<int>(args[0])));
                complete(juce::var());
            })

        // --- Combination memory (general pistons) -------------------------------
        // The processor owns these now. The page used to keep CAPTURED/GENERALS in
        // JavaScript, which made a piston unreachable by a MIDI program change and
        // meant closing the window threw the memory away.
        .withNativeFunction("caeciliaCaptureGeneral",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (! args.isEmpty())
                    proc.captureGeneral(static_cast<std::size_t>(static_cast<int>(args[0])));
                complete(juce::var());
            })
        .withNativeFunction("caeciliaRecallGeneral",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (! args.isEmpty())
                    proc.recallGeneral(static_cast<std::size_t>(static_cast<int>(args[0])));
                complete(juce::var());
            })
        .withNativeFunction("caeciliaClearGeneral",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (! args.isEmpty())
                    proc.clearGeneral(static_cast<std::size_t>(static_cast<int>(args[0])));
                complete(juce::var());
            })
        // --- Tremulant ----------------------------------------------------------
        // console.html has invoked this name since it was written, and it was never
        // registered here: the call was silently dropped, so the tremulant switch
        // has only ever moved on-screen state.
        //
        // @todo Registered, but not yet audible. The command reaches the engine and
        // AudioEngine::applyCommand has SetTremulant as an unhandled case, because
        // the engine's wind supply is null -- nothing calls setWindSupply anywhere
        // in the tree, so the entire wind model (implemented and unit-tested) is
        // inert. Binding it is what finishes this.
        .withNativeFunction("caeciliaSetTremulant",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                proc.setUiTremulant(args.isEmpty() ? false : static_cast<bool>(args[0]));
                complete(juce::var());
            })
        // --- Master EQ: band gain (0..4) and enable, from the Settings panel -----
        .withNativeFunction("caeciliaSetEq",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                    proc.setUiEqGain(static_cast<int>(args[0]), static_cast<float>(static_cast<double>(args[1])));
                complete(juce::var());
            })
        .withNativeFunction("caeciliaEqGesture",
                            [this](const juce::Array<juce::var>& a,
                                   juce::WebBrowserComponent::NativeFunctionCompletion c)
                            {
                                if (a.size() >= 2)
                                    processor_.setUiEqGesture(static_cast<int>(a[0]),
                                                              static_cast<bool>(a[1]));
                                c(juce::var());
                            })
        .withNativeFunction("caeciliaSetEqEnabled",
            [&proc](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                proc.setUiEqEnabled(args.isEmpty() ? true : static_cast<bool>(args[0]));
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
    // The installer records the chosen language under HKCU\Software\Caecilia; read
    // it so the console can open in that language on first run (Windows only).
   #if JUCE_WINDOWS
    installerLang_ = juce::WindowsRegistry::getValue(
        "HKEY_CURRENT_USER\\Software\\Caecilia\\Language", juce::String());
   #endif

    // NOTE: there used to be a first-run block here that reached into
    // juce::StandalonePluginHolder to switch the user's audio device type to
    // WASAPI Exclusive, on the theory that Windows' shared-mode mixer was the
    // cause of the distortion.
    //
    // It is gone for three reasons. It never compiled: StandalonePluginHolder is
    // declared only in the Standalone wrapper translation unit, while this editor
    // is part of the SHARED code target, so the Windows build failed outright on
    // it. It was the wrong remedy: the distortion came from the master limiter
    // reading its look-ahead ring at a wrapped offset (see Limiter::prepare),
    // which shared mode had nothing to do with. And it was the wrong behaviour:
    // silently changing which audio device a user's machine is on, behind their
    // back, is not ours to do.
    //
    // The console already receives an `isStandalone` flag (see timerCallback) and
    // can offer the WASAPI Exclusive hint as advice the user chooses to take.

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

void CaeciliaEditor::pushOrganSpec()
{
    // The instrument itself, so the console can lay out the jambs it really has
    // rather than the ones it was drawn with. Every stop carries its StopId --
    // which is also its host-parameter slot -- so a click, an automation move and
    // a saved session all name the same thing.
    const model::Organ& organ = processor_.organ();

    juce::Array<juce::var> divisions;
    for (const model::Division& d : organ.divisions())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("id",       static_cast<int>(d.id().value));
        o->setProperty("name",     juce::String(d.name()));
        o->setProperty("pedal",    d.kind() == model::DivisionKind::Pedal);
        o->setProperty("enclosed", d.isEnclosed());
        o->setProperty("low",      static_cast<int>(d.lowNote()));
        o->setProperty("high",     static_cast<int>(d.highNote()));
        divisions.add(juce::var(o));
    }

    // The couplers this instrument declares. The index is also the host-parameter
    // slot, exactly as a stop's id is, so the console can draw one by number.
    juce::Array<juce::var> couplers;
    for (std::size_t i = 0; i < organ.couplers().size(); ++i)
    {
        const model::Coupler& c = organ.couplers()[i];
        auto* o = new juce::DynamicObject();
        o->setProperty("idx",   static_cast<int>(i));
        o->setProperty("name",  juce::String(c.name()));
        o->setProperty("from",  static_cast<int>(c.from().value));
        o->setProperty("to",    static_cast<int>(c.to().value));
        o->setProperty("shift", static_cast<int>(c.octaveShiftSemitones()));
        couplers.add(juce::var(o));
    }

    juce::Array<juce::var> stops;
    for (const model::Stop& s : organ.stops())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("id",       static_cast<int>(s.id().value));
        o->setProperty("div",      static_cast<int>(s.division().value));
        o->setProperty("name",     juce::String(s.name()));
        o->setProperty("fam",      static_cast<int>(s.family()));
        o->setProperty("num",      s.footage().num);
        o->setProperty("den",      s.footage().den);
        o->setProperty("role",     static_cast<int>(s.role()));
        o->setProperty("pc",       static_cast<int>(s.pitchClass()));
        o->setProperty("compound", s.isCompound());
        stops.add(juce::var(o));
    }

    auto* spec = new juce::DynamicObject();
    spec->setProperty("divisions", divisions);
    spec->setProperty("stops",     stops);
    spec->setProperty("couplers",  couplers);
    spec->setProperty("playDiv",   static_cast<int>(processor_.playDivision().value));
    web_.emitEventIfBrowserIsVisible("caeciliaOrgan", juce::var(spec));
}

void CaeciliaEditor::pushRegistration()
{
    const std::uint64_t drawn = processor_.drawnStops();

    juce::Array<juce::var> ids;
    for (int i = 0; i < 64; ++i)
        if ((drawn & (std::uint64_t{1} << i)) != 0)
            ids.add(i);

    auto* o = new juce::DynamicObject();
    o->setProperty("stops", ids);
    web_.emitEventIfBrowserIsVisible("caeciliaRegistration", juce::var(o));
}

void CaeciliaEditor::timerCallback()
{
    // Push one consistent frame (lit keys + meters) to the page. The console listens
    // for "caeciliaState" and updates its key lights and VU from it. The wind field
    // is sent too but is always zero (AudioEngine::stepWind() is an empty stub, and
    // captureMeters never fills the wind members), so the console's wind gauges are
    // animated by its own JS curve from the voice count.
    const ui::ConsoleFrame frame = processor_.stateMirror().read();

    auto* obj = new juce::DynamicObject();
    obj->setProperty("voices",   static_cast<int>(frame.meters.activeVoices));
    obj->setProperty("masterPeak", frame.meters.master.peak);
    // Real per-channel output peak (post soft-clip) so the console VU moves with
    // the actual audio, including notes played on the physical MIDI keyboard.
    obj->setProperty("peakL",    processor_.outputPeakL());
    obj->setProperty("peakR",    processor_.outputPeakR());
    obj->setProperty("windSag",  frame.meters.windSagNorm);
    obj->setProperty("playDiv",  static_cast<int>(processor_.playDivision().value));
    // The installer's chosen language (empty if none) — the console applies it on
    // first receipt if the user has not already picked a language in-app.
    obj->setProperty("installerLang", installerLang_);
    // Standalone-only: lets the console offer the "use WASAPI Exclusive if you hear
    // distortion" hint, which is meaningless inside a DAW (the host owns audio).
   #if JucePlugin_Build_Standalone
    obj->setProperty("isStandalone", true);
   #else
    obj->setProperty("isStandalone", false);
   #endif

    // Lit keys of the primary manual, as a compact [note,...] array (source != Off).
    juce::Array<juce::var> lit;
    const auto div = processor_.playDivision().value;
    for (int n = 0; n < 128; ++n)
        if (frame.keys.get(div, static_cast<core::MidiNote>(n)) != ui::KeySource::Off)
            lit.add(n);
    obj->setProperty("litDiv", static_cast<int>(div));
    obj->setProperty("lit", lit);

    // The EQ, as the PARAMETERS hold it. The console keeps a localStorage copy and
    // used to push it at the plugin shortly after every open -- which, now that a
    // host can automate and save these, would overwrite the session's own values
    // with a browser cache every time the window was shown. It adopts these
    // instead, and its copy becomes what it always should have been: a cache.
    juce::Array<juce::var> eq;
    for (int b = 0; b < static_cast<int>(dsp::MasterEq::kBands); ++b)
        eq.add(processor_.uiEqGain(b));
    obj->setProperty("eq", eq);
    obj->setProperty("eqOn", processor_.uiEqEnabled());

    // The drawn registration, as two 32-bit halves because a JS number cannot
    // carry 64 bits of integer precision. This is what lets a host automating a
    // drawstop light that drawstop on the console within a frame, rather than the
    // console and the instrument quietly disagreeing until somebody clicks.
    const std::uint64_t drawn = processor_.drawnStops();
    obj->setProperty("regLo", static_cast<int>(static_cast<std::uint32_t>(drawn & 0xFFFFFFFFu)));
    obj->setProperty("regHi", static_cast<int>(static_cast<std::uint32_t>(drawn >> 32)));

    // The drawn couplers. Sixteen slots fit a JS number exactly, so unlike the
    // registration this needs no splitting into halves.
    obj->setProperty("couplers", static_cast<int>(processor_.drawnCouplers()));

    // Which pistons hold something, so the jamb can show an empty one as empty.
    // Only the eight the console draws: sending 128 bits per frame to light eight
    // buttons is a frame's worth of traffic for nothing.
    int stored = 0;
    for (int i = 0; i < 8; ++i)
        if (processor_.generalIsSet(static_cast<std::size_t>(i)))
            stored |= (1 << i);
    obj->setProperty("genSet", stored);

    // The piston a MIDI program change just fired, so the console can flash it. It
    // is CONSUMED by reading, so the flash happens once rather than for as long as
    // the registration stands.
    obj->setProperty("genFired", processor_.consumeLastGeneral());

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

void CaeciliaEditor::parentHierarchyChanged()
{
    // In the Standalone build the editor lives inside a juce::DocumentWindow whose
    // default title bar has only minimise + close. Add the maximise button and make
    // the window resizable so the user can maximise/fill the screen. In a plugin
    // host there is no DocumentWindow ancestor, so this is a no-op.
    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
    {
        dw->setResizable(true, false);
        dw->setTitleBarButtonsRequired(juce::DocumentWindow::minimiseButton
                                     | juce::DocumentWindow::maximiseButton
                                     | juce::DocumentWindow::closeButton,
                                       false);
    }
}

} // namespace caecilia::plugin
