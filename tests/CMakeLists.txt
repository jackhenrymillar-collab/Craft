include(Catch)

add_executable(foundry_tests
    test_bessel.cpp
    test_membrane.cpp
    test_voice.cpp
    test_treatments.cpp
    test_realtime.cpp
    test_parts.cpp
    test_strikers.cpp
    test_couplings.cpp
    test_kick.cpp
    test_helpers.h)
target_link_libraries(foundry_tests PRIVATE foundry_core Catch2::Catch2WithMain)
foundry_set_warnings(foundry_tests)
target_compile_options(foundry_tests PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-double-promotion -Wno-conversion>)
catch_discover_tests(foundry_tests)
