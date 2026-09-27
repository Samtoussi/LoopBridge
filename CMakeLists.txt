cmake_minimum_required(VERSION 3.22)

project(LoopBridge VERSION 0.1.0)

add_subdirectory(JUCE)
add_subdirectory(SignalsmithStretch)

juce_add_plugin(LoopBridge
    COMPANY_NAME "Versa"
    BUNDLE_ID "com.versa.loopbridge"
    PLUGIN_MANUFACTURER_CODE Vrsa
    PLUGIN_CODE LpBg

    FORMATS VST3
    PRODUCT_NAME "LoopBridge"

    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    COPY_PLUGIN_AFTER_BUILD TRUE
)

juce_generate_juce_header(LoopBridge)

target_sources(
    LoopBridge
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_features(
    LoopBridge
    PRIVATE
        cxx_std_17
)

target_compile_definitions(
    LoopBridge
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(
    LoopBridge
    PRIVATE
        juce::juce_audio_utils
)

juce_add_gui_app(
    LoopBridgeDesktop
    PRODUCT_NAME "LoopBridge"
    COMPANY_NAME "Versa"
    BUNDLE_ID "com.versa.loopbridge.desktop"
)

juce_generate_juce_header(LoopBridgeDesktop)

target_sources(
    LoopBridgeDesktop
    PRIVATE
        Desktop/Main.cpp
)

target_compile_features(
    LoopBridgeDesktop
    PRIVATE
        cxx_std_17
)

target_compile_definitions(
    LoopBridgeDesktop
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_include_directories(
    LoopBridgeDesktop
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/SignalsmithStretch/include
)

target_link_libraries(
    LoopBridgeDesktop
    PRIVATE
        juce::juce_gui_extra
        juce::juce_events
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_devices
        signalsmith-stretch
)