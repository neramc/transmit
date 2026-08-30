# Failures that are real, common on removable media, and nearly impossible to
# arrange by hand. Kept in their own binary and under their own ctest label so
# they can be run alone: "ctest -L fault".

add_executable(transmit_fault_tests FaultInjectionTest.cpp)

target_link_libraries(transmit_fault_tests
    PRIVATE Transmit::Format Transmit::Warnings GTest::gtest GTest::gtest_main)

# The shared temporary-directory helper is included as
# "support/TempDirectory.h", so the tests directory itself is on the
# include path.
target_include_directories(transmit_fault_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

gtest_discover_tests(transmit_fault_tests
    PROPERTIES LABELS fault
    DISCOVERY_TIMEOUT 60)
