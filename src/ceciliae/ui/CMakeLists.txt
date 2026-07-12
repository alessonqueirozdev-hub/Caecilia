# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Ceciliae is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# ui module : the first-class UI pillar (benchmark Organteq).
#
# One of only two modules allowed to include JUCE (the other is `plugin`). It
# contributes its sources to the `Ceciliae` JUCE plugin target rather than to the
# pure `ceciliae_core` static library. The three-layer split (data-driven
# ConsoleLayoutModel, lock-free StateMirror, pluggable ISkin) means both console
# modes -- PhotorealConsole and FlatConsole -- are skins over ONE model and ONE
# live-state feed, so live feedback, MIDI-learn, zoom/pan, theming and
# accessibility are built exactly once.
#
# The `Ceciliae` target is created by the `plugin` module (juce_add_plugin) and
# is added to the build BEFORE this directory, so it exists here. If the plugin
# has not been scaffolded yet, the sources are skipped with a status note so the
# core-only / test builds remain unaffected.
# ---------------------------------------------------------------------------

if(NOT TARGET Ceciliae)
    message(STATUS "Ceciliae ui: 'Ceciliae' plugin target not present yet; ui sources skipped.")
    return()
endif()

target_sources(Ceciliae PRIVATE
    # --- Foundation / theming -------------------------------------------------
    ThemeManager.cpp
    CeciliaeLookAndFeel.cpp
    # --- Three-layer console core --------------------------------------------
    ConsoleLayoutModel.cpp
    ViewportController.cpp
    ConsoleView.cpp
    FlatConsole.cpp
    PhotorealConsole.cpp
    # --- Shared widgets -------------------------------------------------------
    ManualKeyboardComponent.cpp
    DrawstopComponent.cpp
    CouplerRail.cpp
    CombinationBar.cpp
    ExpressionPedal.cpp
    VuMeter.cpp
    WindPressureMeter.cpp
    RegistrationPanel.cpp
    # --- Overlays / accessibility --------------------------------------------
    MidiLearnOverlay.cpp
    ConsoleAccessibilityHandler.cpp)

# The pure logic and shared vocabulary come from ceciliae_core; JUCE provides the
# GUI. The plugin target already links these, but state them explicitly so the ui
# module is self-describing and order-independent.
target_link_libraries(Ceciliae PRIVATE ceciliae::core)

foreach(_jmod juce_gui_basics juce_gui_extra juce_graphics)
    if(TARGET juce::${_jmod})
        target_link_libraries(Ceciliae PRIVATE juce::${_jmod})
    endif()
endforeach()

# Public headers, consumed project-wide via the include root (<root>/src):
#   #include "ceciliae/ui/ConsoleView.h"
# Listed for IDE discoverability and to carry the module's contract.
set(CECILIAE_UI_PUBLIC_HEADERS
    UiGeometry.h
    Theme.h
    ThemeManager.h
    JuceInterop.h
    CeciliaeLookAndFeel.h
    ConsoleLayoutModel.h
    StateMirror.h
    ISkin.h
    FlatConsole.h
    PhotorealConsole.h
    ViewportController.h
    ConsoleView.h
    ManualKeyboardComponent.h
    DrawstopComponent.h
    CouplerRail.h
    CombinationBar.h
    ExpressionPedal.h
    VuMeter.h
    WindPressureMeter.h
    RegistrationPanel.h
    MidiLearnOverlay.h
    ConsoleAccessibilityHandler.h)

target_sources(Ceciliae PRIVATE ${CECILIAE_UI_PUBLIC_HEADERS})
