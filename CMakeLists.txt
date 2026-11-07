cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel). Must be set before project(); ignored elsewhere.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(MyDAW VERSION 0.1.0 LANGUAGES C CXX)

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

set(MYDAW_CORE_SOURCES
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

set(MYDAW_UI_SOURCES
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

set(MYDAW_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# JACK support on Linux (libjack is loaded at run time; only the headers are needed to build)
if(UNIX AND NOT APPLE)
    find_path(JACK_INCLUDE_DIR jack/jack.h)
    if(JACK_INCLUDE_DIR)
        list(APPEND MYDAW_DEFINITIONS JUCE_JACK=1)
    endif()
endif()

set(MYDAW_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_gui_extra)

juce_add_gui_app(MyDAW
    PRODUCT_NAME "myDAW"
    COMPANY_NAME "myDAW"
    BUNDLE_ID "com.mydaw.app"
    VERSION ${PROJECT_VERSION}
    NEEDS_CURL TRUE                    # model download on Linux
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(MyDAW PRIVATE ${MYDAW_CORE_SOURCES} ${MYDAW_UI_SOURCES} Source/Main.cpp)

target_include_directories(MyDAW PRIVATE Source)

target_compile_definitions(MyDAW PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MyDAW,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MyDAW,JUCE_VERSION>"
    ${MYDAW_DEFINITIONS})

target_link_libraries(MyDAW
    PRIVATE
        demucs
        ${MYDAW_JUCE_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Integration tests (engine, sampler, plug-in hosting, file formats)
# ---------------------------------------------------------------------------
option(MYDAW_BUILD_TESTS "Build the myDAW integration tests" OFF)

if(MYDAW_BUILD_TESTS)
    juce_add_console_app(MyDAWTests PRODUCT_NAME "myDAWTests" NEEDS_CURL TRUE)
    target_sources(MyDAWTests PRIVATE ${MYDAW_CORE_SOURCES} Tests/IntegrationTests.cpp)
    target_include_directories(MyDAWTests PRIVATE Source)
    target_compile_definitions(MyDAWTests PRIVATE ${MYDAW_DEFINITIONS})
    target_link_libraries(MyDAWTests
        PRIVATE demucs ${MYDAW_JUCE_MODULES}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
