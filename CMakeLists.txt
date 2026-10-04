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
        Desktop/GmailClient.h
        Desktop/GmailClient.cpp
        Desktop/LoopItem.h
        Desktop/MetadataParser.h
        Desktop/MetadataParser.cpp
        Desktop/PreviewRenderer.h
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
        juce::juce_cryptography
        signalsmith-stretch
        ws2_32
)

# ------------------------------------------------------------
# Metadata parser tests
# ------------------------------------------------------------

juce_add_console_app(
    LoopBridgeMetadataTests
    PRODUCT_NAME "LoopBridge Metadata Tests"
)

juce_generate_juce_header(
    LoopBridgeMetadataTests
)

target_sources(
    LoopBridgeMetadataTests
    PRIVATE
        Desktop/MetadataParserTests.cpp
        Desktop/MetadataParser.cpp
        Desktop/MetadataParser.h
        Desktop/LoopItem.h
)

target_compile_features(
    LoopBridgeMetadataTests
    PRIVATE
        cxx_std_17
)

target_compile_definitions(
    LoopBridgeMetadataTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(
    LoopBridgeMetadataTests
    PRIVATE
        juce::juce_core
)

juce_add_console_app(LoopBridgePreviewTests PRODUCT_NAME "LoopBridge Preview Tests")
juce_generate_juce_header(LoopBridgePreviewTests)
target_sources(LoopBridgePreviewTests PRIVATE Desktop/PreviewRendererTests.cpp Desktop/PreviewRenderer.h)
target_compile_features(LoopBridgePreviewTests PRIVATE cxx_std_17)
target_compile_definitions(LoopBridgePreviewTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_include_directories(LoopBridgePreviewTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/SignalsmithStretch/include)
target_link_libraries(LoopBridgePreviewTests PRIVATE
    juce::juce_audio_formats juce::juce_cryptography signalsmith-stretch)

option(LOOPBRIDGE_BUILD_GMAIL_TESTS "Build offline Gmail worker tests" OFF)
if(LOOPBRIDGE_BUILD_GMAIL_TESTS)
    juce_add_console_app(LoopBridgeGmailTests PRODUCT_NAME "LoopBridge Gmail Tests")
    juce_generate_juce_header(LoopBridgeGmailTests)
    target_sources(LoopBridgeGmailTests PRIVATE Desktop/GmailClientTests.cpp Desktop/GmailClient.cpp)
    target_compile_features(LoopBridgeGmailTests PRIVATE cxx_std_17)
    target_compile_definitions(LoopBridgeGmailTests PRIVATE
        LOOPBRIDGE_GMAIL_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(LoopBridgeGmailTests PRIVATE juce::juce_events juce::juce_cryptography)
    if(WIN32)
        target_link_libraries(LoopBridgeGmailTests PRIVATE ws2_32)
    endif()
endif()
