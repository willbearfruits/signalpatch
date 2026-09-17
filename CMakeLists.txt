cmake_minimum_required(VERSION 3.22)

project(signalpatch VERSION 0.3.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SIGNALPATCH_ENABLE_ASIO "Enable JUCE's ASIO backend on Windows" OFF)
option(SIGNALPATCH_ALLOW_JUCE_FETCH
       "Fetch the pinned JUCE release when no matching package is installed"
       ON)
# The neural amp is matrix arithmetic: with AVX2 and FMA a WaveNet costs about a
# quarter less. x86-64-v3 means any Intel since Haswell (2013) or AMD since
# Excavator / Zen. Set it to "" for older machines, or to "native" for this one.
# It applies to every target so Eigen's alignment is the same everywhere.
set(SIGNALPATCH_CPU_BASELINE "x86-64-v3" CACHE STRING "x86-64 instruction set level to build for (empty = the compiler's default)")
if(SIGNALPATCH_CPU_BASELINE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    if(MSVC)
        if(NOT SIGNALPATCH_CPU_BASELINE STREQUAL "x86-64" AND NOT SIGNALPATCH_CPU_BASELINE STREQUAL "x86-64-v2")
            add_compile_options(/arch:AVX2)
        endif()
    else()
        add_compile_options(-march=${SIGNALPATCH_CPU_BASELINE})
    endif()
endif()

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
    JUCE_VST3_CAN_REPLACE_VST2=0)
# The app talks https to tone3000.com; JUCE's own Linux HTTP has no TLS, libcurl
# (loaded at runtime) does. Everything else stays off curl.
set(SIGNALPATCH_NO_CURL JUCE_USE_CURL=0)
set(SIGNALPATCH_APP_CURL JUCE_USE_CURL=0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(CURL QUIET)
    if(TARGET CURL::libcurl)
        set(SIGNALPATCH_APP_CURL JUCE_USE_CURL=1)
    else()
        message(WARNING "SignalPatch: libcurl not found - TONE3000 (https) will be unavailable in the app")
    endif()
endif()

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
            PATCH_COMMAND ${CMAKE_COMMAND}
                -DGIT=${GIT_EXECUTABLE}
                -DPATCH=${CMAKE_CURRENT_SOURCE_DIR}/packaging/patches/namcore-nonatomic-shared-ptr.patch
                -P ${CMAKE_CURRENT_SOURCE_DIR}/packaging/patches/apply-namcore-patch.cmake)
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
        target_compile_options(nam_core PRIVATE /w) # optimization comes from the config
    else()
        target_compile_options(nam_core PRIVATE -w -O3)
    endif()
    set(SIGNALPATCH_HAS_NAM TRUE)
else()
    message(STATUS "SignalPatch: NAM disabled; Neural Amp node stays a passthrough")
endif()

# --- The original JUCE rack. Retired in 0.3 but kept buildable for one
# release: -DSIGNALPATCH_BUILD_JUCE_UI=ON brings it back next to the GLFW app.
option(SIGNALPATCH_BUILD_JUCE_UI "Build the retired JUCE rack UI (signalpatch_juce)" OFF)
if(SIGNALPATCH_BUILD_JUCE_UI)
    juce_add_gui_app(signalpatch_juce
        PRODUCT_NAME "SignalPatch (JUCE)"
        COMPANY_NAME "SignalPatch"
        VERSION "${PROJECT_VERSION}")

    juce_generate_juce_header(signalpatch_juce)

    target_sources(signalpatch_juce PRIVATE
        src/Main.cpp
        ${SIGNALPATCH_AUDIO_SOURCES}
        ${SIGNALPATCH_UI_SOURCES})

    target_compile_features(signalpatch_juce PRIVATE cxx_std_20)
    target_compile_definitions(signalpatch_juce PRIVATE ${SIGNALPATCH_COMMON_DEFINITIONS} ${SIGNALPATCH_NO_CURL})

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(signalpatch_juce PRIVATE JUCE_ALSA=1 JUCE_JACK=1)
    elseif(WIN32)
        target_compile_definitions(signalpatch_juce PRIVATE JUCE_WASAPI=1)
        if(SIGNALPATCH_ENABLE_ASIO)
            target_compile_definitions(signalpatch_juce PRIVATE JUCE_ASIO=1)
        else()
            target_compile_definitions(signalpatch_juce PRIVATE JUCE_ASIO=0)
        endif()
    endif()

    if(SIGNALPATCH_HAS_NAM)
        target_compile_definitions(signalpatch_juce PRIVATE SIGNALPATCH_HAS_NAM=1)
        # Whole-archive: NAM architectures (WaveNet, LSTM, ...) register their
        # config parsers through static initializers the linker must not drop.
        target_link_libraries(signalpatch_juce PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
    endif()

    target_link_libraries(signalpatch_juce
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_devices
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_gui_extra
            juce::juce_opengl
            juce::juce_data_structures
            juce::juce_osc
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

enable_testing()

juce_add_console_app(signalpatch_tests
    PRODUCT_NAME "signalpatch_tests")

juce_generate_juce_header(signalpatch_tests)

target_sources(signalpatch_tests PRIVATE
    tests/EngineTests.cpp
    src/audio/Graph.cpp
    src/audio/RenderPool.cpp
    src/audio/PatchHistory.cpp
    src/audio/PatchBundle.cpp
    src/audio/MidiMap.cpp
    src/audio/ControllerFeedback.cpp
    src/audio/Tone3000.cpp
    src/audio/Graph.h
    src/audio/Processors.cpp
    src/audio/Processors.h)

target_compile_features(signalpatch_tests PRIVATE cxx_std_20)
target_compile_definitions(signalpatch_tests PRIVATE ${SIGNALPATCH_COMMON_DEFINITIONS} ${SIGNALPATCH_NO_CURL})
target_compile_definitions(signalpatch_tests PRIVATE SIGNALPATCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}") # the example patches are loaded by a test
if(NOT MSVC)
    # The tests replace global new/delete for the allocation trap; GCC cannot
    # see the malloc/free pairing is internally consistent.
    target_compile_options(signalpatch_tests PRIVATE -Wno-mismatched-new-delete)
endif()

if(SIGNALPATCH_HAS_NAM)
    target_compile_definitions(signalpatch_tests PRIVATE SIGNALPATCH_HAS_NAM=1)
    target_link_libraries(signalpatch_tests PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
endif()

target_link_libraries(signalpatch_tests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_cryptography
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
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/io.github.willbearfruits.SignalPatch.desktop
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES packaging/linux/io.github.willbearfruits.SignalPatch.metainfo.xml
            DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
    install(FILES packaging/linux/io.github.willbearfruits.SignalPatch.svg
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
endif()

# --- SignalPatch: GLFW + OpenGL + NanoVG app over the engine (was "SignalPatch 2") ---
find_package(glfw3 3.3 QUIET)
find_package(OpenGL QUIET)
option(SIGNALPATCH_BUILD_V2 "Build the GLFW/OpenGL app (signalpatch)" ON)
if(SIGNALPATCH_BUILD_V2 AND NOT TARGET glfw AND TARGET OpenGL::GL)
    # No system GLFW (Windows, minimal images): build the pinned release in-tree.
    include(FetchContent)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
        URL_HASH SHA256=b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9)
    FetchContent_MakeAvailable(glfw)
    set(glfw3_VERSION "3.4 (fetched)")
endif()
if(SIGNALPATCH_BUILD_V2 AND TARGET glfw AND TARGET OpenGL::GL)
    include(FetchContent)
    FetchContent_Declare(nanovg
        GIT_REPOSITORY https://github.com/memononen/nanovg.git
        GIT_TAG ce3bf745eb2d2dbc14a50bf2446783f691ac4353)
    FetchContent_MakeAvailable(nanovg) # no CMakeLists upstream: populate only

    file(GLOB SIGNALPATCH_V2_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/v2/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/v2/*.h")

    juce_add_console_app(signalpatch PRODUCT_NAME "SignalPatch")
    set_target_properties(signalpatch PROPERTIES OUTPUT_NAME "SignalPatch") # keep the documented executable name
    juce_generate_juce_header(signalpatch)
    target_sources(signalpatch PRIVATE
        ${SIGNALPATCH_V2_SOURCES}
        ${SIGNALPATCH_AUDIO_SOURCES}
        "${nanovg_SOURCE_DIR}/src/nanovg.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/src/gl.c")
    target_include_directories(signalpatch SYSTEM PRIVATE
        "${nanovg_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include")
    target_compile_features(signalpatch PRIVATE cxx_std_20)
    target_compile_definitions(signalpatch PRIVATE ${SIGNALPATCH_COMMON_DEFINITIONS} ${SIGNALPATCH_APP_CURL}
        JUCE_MODAL_LOOPS_PERMITTED=1) # runDispatchLoopUntil: JUCE pumped from the GLFW frame loop
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(signalpatch PRIVATE JUCE_ALSA=1 JUCE_JACK=1)
    elseif(WIN32)
        target_compile_definitions(signalpatch PRIVATE JUCE_WASAPI=1 JUCE_DIRECTSOUND=1
            JUCE_ASIO=$<BOOL:${SIGNALPATCH_ENABLE_ASIO}>)
        # A windowed app: no console window behind the rack, main() kept as the entry point.
        if(MSVC)
            target_link_options(signalpatch PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
        endif()
    endif()
    # The bundled font travels next to the executable (see loadFont in Main.cpp).
    add_custom_command(TARGET signalpatch POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts" "$<TARGET_FILE_DIR:signalpatch>/fonts")
    # So do the example patches (FILE > Examples looks for them there).
    file(GLOB SIGNALPATCH_EXAMPLE_PATCHES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.signalpatch")
    add_custom_command(TARGET signalpatch POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:signalpatch>/examples"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SIGNALPATCH_EXAMPLE_PATCHES} "$<TARGET_FILE_DIR:signalpatch>/examples")
    set_source_files_properties("${nanovg_SOURCE_DIR}/src/nanovg.c" "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/src/gl.c"
        PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
    if(SIGNALPATCH_HAS_NAM)
        target_compile_definitions(signalpatch PRIVATE SIGNALPATCH_HAS_NAM=1)
        target_link_libraries(signalpatch PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
    endif()
    if(TARGET CURL::libcurl)
        target_link_libraries(signalpatch PRIVATE CURL::libcurl)
    endif()
    target_link_libraries(signalpatch
        PRIVATE
            juce::juce_audio_devices
            juce::juce_cryptography
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_graphics
            juce::juce_data_structures
            juce::juce_events
            glfw
            OpenGL::GL
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    message(STATUS "SignalPatch: building signalpatch (GLFW ${glfw3_VERSION})")
else()
    message(STATUS "SignalPatch: signalpatch skipped (needs glfw3 and OpenGL)")
endif()

if(TARGET signalpatch)
    install(TARGETS signalpatch RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY external/fonts/ DESTINATION "${CMAKE_INSTALL_DATADIR}/signalpatch/fonts")
    install(DIRECTORY examples/ DESTINATION "${CMAKE_INSTALL_DATADIR}/signalpatch/examples" FILES_MATCHING PATTERN "*.signalpatch")
endif()
