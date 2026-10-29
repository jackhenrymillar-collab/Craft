# Third-party dependencies, all pinned to release tags.
# Check upstream for newer tags before bumping; don't guess.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(FOUNDRY_JUCE_TAG   "9.0.4"   CACHE STRING "JUCE release tag")
set(FOUNDRY_CATCH2_TAG "v3.16.1" CACHE STRING "Catch2 release tag")

if(FOUNDRY_BUILD_PLUGIN OR FOUNDRY_BUILD_TOOLS)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${FOUNDRY_JUCE_TAG}
        GIT_SHALLOW    ON
        GIT_PROGRESS   ON)
    FetchContent_MakeAvailable(juce)
endif()

if(FOUNDRY_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        ${FOUNDRY_CATCH2_TAG}
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
