juce_add_console_app(render_cli PRODUCT_NAME "render_cli")

target_sources(render_cli PRIVATE main.cpp Analysis.cpp)
target_compile_definitions(render_cli PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(render_cli
    PRIVATE
        foundry_core
        foundry_fonts
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
