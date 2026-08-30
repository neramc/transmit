# Property tests: one invariant, run over shapes nobody wrote a case
# for. They link the format layer only, so they run without a display and
# without Qt Quick.

add_executable(transmit_property_tests
    RoundTripPropertyTest.cpp
    PathPropertyTest.cpp)

target_link_libraries(transmit_property_tests
    PRIVATE Transmit::Format Transmit::Warnings GTest::gtest GTest::gtest_main)

# The generators are included as "property/Generators.h" and the temporary
# directory helper as "support/TempDirectory.h", so the tests directory itself
# is on the include path.
target_include_directories(transmit_property_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

gtest_discover_tests(transmit_property_tests
    PROPERTIES LABELS property DISCOVERY_TIMEOUT 120)
