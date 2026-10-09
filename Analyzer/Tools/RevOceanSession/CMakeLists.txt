cmake_minimum_required(VERSION 3.22)

# The reference is measured on Apple silicon; the campaign's captures are arm64.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Architecture of the session host")

project(RevOceanSession VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# JUCE 8.0.14, the checkout the plug-in and the Amanita Analyzer are built from.
get_filename_component(_revocean_default_juce "${CMAKE_CURRENT_SOURCE_DIR}/../../../../Juce" ABSOLUTE)
set(REVOCEAN_SESSION_JUCE_SOURCE "${_revocean_default_juce}" CACHE PATH "Path to a JUCE source checkout")

if(NOT EXISTS "${REVOCEAN_SESSION_JUCE_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "No JUCE checkout at ${REVOCEAN_SESSION_JUCE_SOURCE}; set REVOCEAN_SESSION_JUCE_SOURCE")
endif()

add_subdirectory("${REVOCEAN_SESSION_JUCE_SOURCE}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

juce_add_gui_app(RevOceanSession
    PRODUCT_NAME "Rev OCEAN Session"
    COMPANY_NAME "Amanita Ocean"
    BUNDLE_ID "audio.amanitaocean.revoceansession"
)

target_sources(RevOceanSession
    PRIVATE
        Source/Job.cpp
        Source/Job.h
        Source/Main.cpp
        Source/Native.h
        Source/Native.mm
        Source/Session.cpp
        Source/Session.h
)

target_compile_definitions(RevOceanSession
    PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RevOceanSession,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:RevOceanSession,JUCE_VERSION>"
)

target_link_libraries(RevOceanSession
    PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_cryptography
        juce::juce_gui_basics
        "-framework CoreAudioKit"   # the Audio Unit host's generic and Cocoa views
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
