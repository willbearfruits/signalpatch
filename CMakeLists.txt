cmake_minimum_required(VERSION 3.22)

project(signalpatch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SIGNALPATCH_ENABLE_ASIO "Enable JUCE's ASIO backend on Windows" OFF)
option(SIGNALPATCH_ALLOW_JUCE_FETCH
       "Fetch the pinned JUCE release when no matching package is installed"
       ON)
option(SIGNALPATCH_ENABLE_NAM "Build the Neural Amp Modeler node against NeuralAmpModelerCore" ON)
set(SIGNALPATCH_NAM_CORE_DIR
    "$ENV{HOME}/Projects/GitHub/external_clones/neural-amp-modeler-plugin-a2/NeuralAmpModelerCore"
    CACHE PATH "Path to a NeuralAmpModelerCore checkout")

# Prefer the installed package on this machine. The fallback keeps clean Linux
# and Windows checkouts reproducible without hard-coding a host path.
find_package(JUCE 8.0.13 EXACT CONFIG QUIET)

if(NOT TARGET juce::juce_core)
    if(NOT SIGNALPATCH_ALLOW_JUCE_FETCH)
        message(FATAL_ERROR
            "JUCE 8.0.13 was not found. Set JUCE_DIR or enable "
            "SIGNALPATCH_ALLOW_JUCE_FETCH.")
    endif()

    include(FetchContent)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.13
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

file(GLOB_RECURSE SIGNALPATCH_AUDIO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/*.h")

file(GLOB_RECURSE SIGNALPATCH_UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h")

set(SIGNALPATCH_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# --- Neural Amp Modeler core (third-party, compiled quietly as a static lib).
# Source resolution order: a local checkout (SIGNALPATCH_NAM_CORE_DIR), then a
# pinned upstream fetch. Both get the small guard patch shipped in
# packaging/patches (GCC 11's libstdc++ lacks std::atomic<std::shared_ptr>;
# the patch widens NAM's existing libc++ fallback guard to an opt-in macro).
set(SIGNALPATCH_HAS_NAM FALSE)
set(SIGNALPATCH_NAM_SOURCE_DIR "")
if(SIGNALPATCH_ENABLE_NAM)
    if(EXISTS "${SIGNALPATCH_NAM_CORE_DIR}/NAM/dsp.h")
        set(SIGNALPATCH_NAM_SOURCE_DIR "${SIGNALPATCH_NAM_CORE_DIR}")
        message(STATUS "SignalPatch: NAM core from local checkout ${SIGNALPATCH_NAM_CORE_DIR}")
    else()
        include(FetchContent)
        find_package(Git REQUIRED)
        FetchContent_Declare(namcore
            GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
            GIT_TAG ab72c078cd141be60d963d357bd501d56cc9606b
            PATCH_COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace
                "${CMAKE_CURRENT_SOURCE_DIR}/packaging/patches/namcore-nonatomic-shared-ptr.patch")
        FetchContent_GetProperties(namcore)
        if(NOT namcore_POPULATED)
            FetchContent_Populate(namcore)
        endif()
        set(SIGNALPATCH_NAM_SOURCE_DIR "${namcore_SOURCE_DIR}")
        message(STATUS "SignalPatch: NAM core fetched to ${SIGNALPATCH_NAM_SOURCE_DIR}")
    endif()
endif()

if(NOT SIGNALPATCH_NAM_SOURCE_DIR STREQUAL "")
    file(GLOB NAM_CORE_SOURCES
        "${SIGNALPATCH_NAM_SOURCE_DIR}/NAM/*.cpp"
        "${SIGNALPATCH_NAM_SOURCE_DIR}/NAM/wavenet/*.cpp")
    add_library(nam_core STATIC ${NAM_CORE_SOURCES})
    target_include_directories(nam_core SYSTEM PUBLIC
        "${SIGNALPATCH_NAM_SOURCE_DIR}"
        "${SIGNALPATCH_NAM_SOURCE_DIR}/Dependencies/eigen"
        "${SIGNALPATCH_NAM_SOURCE_DIR}/Dependencies/nlohmann")
    target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NAM_NONATOMIC_SHARED_PTR)
    target_compile_features(nam_core PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(nam_core PRIVATE /w /O2)
    else()
        target_compile_options(nam_core PRIVATE -w -O3)
    endif()
    set(SIGNALPATCH_HAS_NAM TRUE)
else()
    message(STATUS "SignalPatch: NAM disabled; Neural Amp node stays a passthrough")
endif()

juce_add_gui_app(signalpatch
    PRODUCT_NAME "SignalPatch"
    COMPANY_NAME "SignalPatch"
    VERSION "${PROJECT_VERSION}")

juce_generate_juce_header(signalpatch)

target_sources(signalpatch PRIVATE
    src/Main.cpp
    ${SIGNALPATCH_AUDIO_SOURCES}
    ${SIGNALPATCH_UI_SOURCES})

target_compile_features(signalpatch PRIVATE cxx_std_20)
target_compile_definitions(signalpatch PRIVATE ${SIGNALPATCH_COMMON_DEFINITIONS})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(signalpatch PRIVATE JUCE_ALSA=1 JUCE_JACK=1)
elseif(WIN32)
    target_compile_definitions(signalpatch PRIVATE JUCE_WASAPI=1)
    if(SIGNALPATCH_ENABLE_ASIO)
        target_compile_definitions(signalpatch PRIVATE JUCE_ASIO=1)
    else()
        target_compile_definitions(signalpatch PRIVATE JUCE_ASIO=0)
    endif()
endif()

if(SIGNALPATCH_HAS_NAM)
    target_compile_definitions(signalpatch PRIVATE SIGNALPATCH_HAS_NAM=1)
    # Whole-archive: NAM architectures (WaveNet, LSTM, ...) register their
    # config parsers through static initializers the linker must not drop.
    target_link_libraries(signalpatch PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
endif()

target_link_libraries(signalpatch
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_data_structures
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

enable_testing()

juce_add_console_app(signalpatch_tests
    PRODUCT_NAME "signalpatch_tests")

juce_generate_juce_header(signalpatch_tests)

target_sources(signalpatch_tests PRIVATE
    tests/EngineTests.cpp
    src/audio/Graph.cpp
    src/audio/Graph.h
    src/audio/Processors.cpp
    src/audio/Processors.h)

target_compile_features(signalpatch_tests PRIVATE cxx_std_20)
target_compile_definitions(signalpatch_tests PRIVATE ${SIGNALPATCH_COMMON_DEFINITIONS})

if(SIGNALPATCH_HAS_NAM)
    target_compile_definitions(signalpatch_tests PRIVATE SIGNALPATCH_HAS_NAM=1)
    target_link_libraries(signalpatch_tests PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
endif()

target_link_libraries(signalpatch_tests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME signalpatch_engine_tests COMMAND signalpatch_tests)

# --- Install rules (used by the flatpak and other packagers).
include(GNUInstallDirs)
install(TARGETS signalpatch RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/io.github.willbearfruits.SignalPatch.desktop
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES packaging/linux/io.github.willbearfruits.SignalPatch.metainfo.xml
            DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
    install(FILES packaging/linux/io.github.willbearfruits.SignalPatch.svg
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
endif()
