cmake_minimum_required(VERSION 3.24)

project(Foundry VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Universal binaries on macOS unless the caller overrides it.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(FOUNDRY_BUILD_PLUGIN "Build the plugin (VST3/AU/Standalone)" ON)
option(FOUNDRY_BUILD_TOOLS  "Build render_cli" ON)
option(FOUNDRY_BUILD_TESTS  "Build the Catch2 test suite" ON)
option(FOUNDRY_SANITIZERS   "Build with address + undefined sanitizers" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)
include(Dependencies)

# ---------------------------------------------------------------------------
# foundry_core: model + compile + dsp. Plain C++20, no JUCE, so the physics
# can be unit-tested and rendered offline without any GUI/plugin machinery.
# ---------------------------------------------------------------------------
add_library(foundry_core STATIC
    source/model/Materials.cpp
    source/model/Treatments.cpp
    source/model/PartCatalogue.cpp
    source/compile/Bessel.cpp
    source/compile/BeamFunctions.cpp
    source/compile/LinearAlgebra.cpp
    source/compile/ModalBasis.cpp
    source/compile/MembraneSolver.cpp
    source/compile/StripSolver.cpp
    source/compile/PlateSolver.cpp
    source/compile/ShellSolver.cpp
    source/compile/TreatmentSolver.cpp
    source/compile/CompiledInstrument.cpp
    source/compile/InstrumentExchange.cpp
    source/compile/CompileWorker.cpp
    source/dsp/ModalBank.cpp
    source/dsp/Striker.cpp
    source/dsp/Calibrate.cpp
    source/dsp/PadVoice.cpp
)
target_include_directories(foundry_core PUBLIC source)
foundry_set_warnings(foundry_core)
foundry_enable_sanitizers(foundry_core)
find_package(Threads REQUIRED)
target_link_libraries(foundry_core PUBLIC Threads::Threads)

if(FOUNDRY_BUILD_PLUGIN OR FOUNDRY_BUILD_TOOLS)
    juce_add_binary_data(foundry_fonts
        NAMESPACE FoundryFonts
        SOURCES
            resources/fonts/IBMPlexSans-Regular.ttf
            resources/fonts/IBMPlexSans-SemiBold.ttf
            resources/fonts/IBMPlexMono-Regular.ttf
            resources/fonts/IBMPlexMono-Medium.ttf)
endif()

if(FOUNDRY_BUILD_PLUGIN)
    add_subdirectory(source/plugin)
endif()

if(FOUNDRY_BUILD_TOOLS)
    add_subdirectory(tools/render_cli)
endif()

if(FOUNDRY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
