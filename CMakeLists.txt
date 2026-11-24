cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel). Must be set before project(); ignored elsewhere.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(Chanco VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Static runtime so the .exe runs without installing the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if -DJUCE_DIR=... is given, otherwise download it.
# ---------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Path to an existing JUCE checkout (optional)")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.12
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# AI stem separation: demucs.cpp (MIT, vendored in ThirdParty/) + Eigen (MPL2)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-configure)   # headers only
FetchContent_MakeAvailable(eigen)

file(GLOB DEMUCS_SOURCES ThirdParty/demucs.cpp/src/*.cpp)
add_library(demucs STATIC ${DEMUCS_SOURCES})
target_include_directories(demucs SYSTEM PUBLIC ThirdParty/demucs.cpp/src ${eigen_SOURCE_DIR})
target_compile_definitions(demucs PUBLIC EIGEN_MPL2_ONLY PRIVATE NDEBUG)
target_compile_features(demucs PUBLIC cxx_std_17)
set_target_properties(demucs PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(demucs PRIVATE /O2 /bigobj /W0 /EHsc)
else()
    target_compile_options(demucs PRIVATE -O3 -w)
endif()

set(CHANCO_CORE_SOURCES
    Source/AI/StemSeparation.cpp
    Source/Session.cpp
    Source/Model/Project.cpp
    Source/Audio/SampleData.cpp
    Source/Audio/SampleEditing.cpp
    Source/Audio/Slicing.cpp
    Source/Audio/EffectChain.cpp
    Source/Audio/Channel.cpp
    Source/Audio/AudioEngine.cpp
    Source/Audio/PluginManager.cpp)

set(CHANCO_UI_SOURCES
    Source/UI/Theme.cpp
    Source/UI/WindowPlacement.cpp
    Source/UI/PluginWindows.cpp
    Source/UI/TransportBar.cpp
    Source/UI/PatternList.cpp
    Source/UI/ChannelSettings.cpp
    Source/UI/SampleEditor.cpp
    Source/UI/Sampler.cpp
    Source/UI/StepGrid.cpp
    Source/UI/PianoRoll.cpp
    Source/UI/PatternEditor.cpp
    Source/UI/SongEditor.cpp
    Source/UI/MainComponent.cpp)

set(CHANCO_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# JACK support on Linux (libjack is loaded at run time; only the headers are needed to build)
if(UNIX AND NOT APPLE)
    find_path(JACK_INCLUDE_DIR jack/jack.h)
    if(JACK_INCLUDE_DIR)
        list(APPEND CHANCO_DEFINITIONS JUCE_JACK=1)
    endif()
endif()

set(CHANCO_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_gui_extra)

juce_add_gui_app(Chanco
    PRODUCT_NAME "Chanco"
    COMPANY_NAME "Chanco"
    BUNDLE_ID "com.chanco.app"
    VERSION ${PROJECT_VERSION}
    NEEDS_CURL TRUE                    # model download on Linux
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(Chanco PRIVATE ${CHANCO_CORE_SOURCES} ${CHANCO_UI_SOURCES} Source/Main.cpp)

target_include_directories(Chanco PRIVATE Source)

target_compile_definitions(Chanco PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Chanco,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Chanco,JUCE_VERSION>"
    ${CHANCO_DEFINITIONS})

target_link_libraries(Chanco
    PRIVATE
        demucs
        ${CHANCO_JUCE_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Integration tests (engine, sampler, plug-in hosting, file formats)
# ---------------------------------------------------------------------------
option(CHANCO_BUILD_TESTS "Build the Chanco integration tests" OFF)

if(CHANCO_BUILD_TESTS)
    juce_add_console_app(ChancoTests PRODUCT_NAME "ChancoTests" NEEDS_CURL TRUE)
    target_sources(ChancoTests PRIVATE ${CHANCO_CORE_SOURCES} Tests/IntegrationTests.cpp)
    target_include_directories(ChancoTests PRIVATE Source)
    target_compile_definitions(ChancoTests PRIVATE ${CHANCO_DEFINITIONS})
    target_link_libraries(ChancoTests
        PRIVATE demucs ${CHANCO_JUCE_MODULES}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
