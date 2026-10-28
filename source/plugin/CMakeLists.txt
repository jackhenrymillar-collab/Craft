juce_add_plugin(Foundry
    COMPANY_NAME "Foundry"
    BUNDLE_ID "com.foundry.foundry"
    PLUGIN_MANUFACTURER_CODE Fndy
    PLUGIN_CODE Fdr1
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Foundry"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Drum Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Foundry PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    Parameters.cpp
    ../ui/theme/Theme.cpp
    ../ui/theme/FoundryLookAndFeel.cpp
    ../ui/common/HeadView.cpp
    ../ui/kit/KitView.cpp)

target_compile_definitions(Foundry PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Foundry
    PRIVATE
        foundry_core
        foundry_fonts
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
