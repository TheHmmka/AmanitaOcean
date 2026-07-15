cmake_minimum_required(VERSION 3.22)

project(AmanitaOcean VERSION 0.10.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 2cdfca8feb300fb424002ba2c2751569e5bacb64 # 8.0.14
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

FetchContent_MakeAvailable(JUCE)

add_library(AmanitaOceanDSP STATIC
    Source/dsp/BloomCharacter.cpp
    Source/dsp/BloomCharacter.h
    Source/dsp/DriftCharacter.cpp
    Source/dsp/DriftCharacter.h
    Source/dsp/FDNReverb.cpp
    Source/dsp/FDNReverb.h
    Source/dsp/SpatialDucker.cpp
    Source/dsp/SpatialDucker.h
    Source/dsp/VeilCharacter.cpp
    Source/dsp/VeilCharacter.h
)

target_include_directories(AmanitaOceanDSP
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)

set_target_properties(AmanitaOceanDSP PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(AmanitaOceanDSP
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

juce_add_plugin(AmanitaOcean
    COMPANY_NAME "Amanita Ocean"
    BUNDLE_ID "audio.amanitaocean.reverb"
    PLUGIN_MANUFACTURER_CODE Aman
    PLUGIN_CODE AmRv
    FORMATS AU VST3
    PRODUCT_NAME "Amanita Ocean"
    DESCRIPTION "Experimental evolving algorithmic reverb"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Reverb Stereo
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(AmanitaOcean
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/ui/CharacterSelector.cpp
        Source/ui/CharacterSelector.h
        Source/ui/OceanLookAndFeel.cpp
        Source/ui/OceanLookAndFeel.h
        Source/ui/ParameterKnob.cpp
        Source/ui/ParameterKnob.h
        Source/ui/PluginEditor.cpp
        Source/ui/PluginEditor.h
)

target_compile_definitions(AmanitaOcean
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(AmanitaOcean
    PRIVATE
        AmanitaOceanDSP
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if(APPLE)
    add_custom_command(TARGET AmanitaOcean_VST3 POST_BUILD
        COMMAND /usr/bin/codesign --force --deep --sign -
                "$<TARGET_BUNDLE_DIR:AmanitaOcean_VST3>"
        COMMENT "Applying local ad-hoc signature to the finished VST3 bundle"
        VERBATIM
    )

    add_custom_command(TARGET AmanitaOcean_AU POST_BUILD
        COMMAND /usr/bin/codesign --force --deep --sign -
                "$<TARGET_BUNDLE_DIR:AmanitaOcean_AU>"
        COMMENT "Applying local ad-hoc signature to the finished AU bundle"
        VERBATIM
    )
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(AmanitaOceanDSPTests
        Tests/DspTests.cpp
    )

    target_link_libraries(AmanitaOceanDSPTests
        PRIVATE
            AmanitaOceanDSP
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(NAME amanita_ocean_dsp COMMAND AmanitaOceanDSPTests)
    set_tests_properties(amanita_ocean_dsp PROPERTIES TIMEOUT 420)

    add_executable(AmanitaOceanStateTests
        Tests/StateTests.cpp
    )

    target_include_directories(AmanitaOceanStateTests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    )

    target_link_libraries(AmanitaOceanStateTests
        PRIVATE
            AmanitaOcean
            juce::juce_audio_utils
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(NAME amanita_ocean_state COMMAND AmanitaOceanStateTests)
    set_tests_properties(amanita_ocean_state PROPERTIES TIMEOUT 30)
endif()
