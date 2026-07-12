# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Ceciliae is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# plugin module : the JUCE VST3 / Standalone wrapper.
#
# This is one of only TWO modules allowed to include JUCE (the other is `ui`).
# It CREATES the `Ceciliae` plugin target via juce_add_plugin and links it to the
# pure `ceciliae_core` static library across the narrow AudioEngine seam. The
# `ui` module (added after this one by the root CMakeLists) contributes its
# sources to the same `Ceciliae` target.
#
# The proprietary / JUCE license boundary is kept at the target level: JUCE is a
# separate third-party dependency (see THIRD_PARTY_NOTICES.md) and is NEVER linked
# into ceciliae_core.
# ---------------------------------------------------------------------------

# Defensive guards: the root only adds this directory when the plugin build is
# enabled and JUCE has been fetched, but keep the module self-protecting.
if(NOT CECILIAE_BUILD_PLUGIN OR CECILIAE_CORE_ONLY)
    return()
endif()

if(NOT COMMAND juce_add_plugin)
    message(WARNING "Ceciliae: JUCE not available; skipping the plugin target.")
    return()
endif()

juce_add_plugin(Ceciliae
    PRODUCT_NAME              "Ceciliae"
    COMPANY_NAME             "Alesson Queiroz"
    BUNDLE_ID                 "com.alessonqueiroz.ceciliae"
    PLUGIN_MANUFACTURER_CODE  Alqz
    PLUGIN_CODE               Ceci
    FORMATS                   VST3 Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           "Instrument"
    DESCRIPTION               "Ceciliae hybrid pipe-organ virtual instrument")

# --- plugin module translation units ---------------------------------------
target_sources(Ceciliae PRIVATE
    ParameterLayout.cpp
    ParameterMirror.cpp
    CommandBridge.cpp
    MeterBridge.cpp
    PluginProcessor.cpp
    PluginEditor.cpp)

# --- compile-time JUCE configuration ---------------------------------------
target_compile_definitions(Ceciliae PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# --- dependencies ----------------------------------------------------------
# ceciliae_core is the pure engine seam; juce_audio_utils transitively brings in
# audio_processors / gui_basics / data_structures / audio_basics.
target_link_libraries(Ceciliae
    PRIVATE
        ceciliae::core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# The project-wide include root so "ceciliae/<module>/<File>.h" resolves. (core's
# PUBLIC include dir already provides this, but state it explicitly for clarity.)
target_include_directories(Ceciliae PRIVATE
    "${CMAKE_SOURCE_DIR}/src")

target_compile_features(Ceciliae PRIVATE cxx_std_20)
