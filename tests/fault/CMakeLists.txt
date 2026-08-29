# Failures that are real, common on removable media, and nearly impossible to
# arrange by hand. Kept in their own binary and under their own ctest label so
# they can be run alone: "ctest -L fault".

add_executable(transmit_fault_tests FaultInjectionTest.cpp)

target_link_libraries(transmit_fault_tests
    PRIVATE Transmit::Format Transmit::Warnings GTest::gtest GTest::gtest_main)

gtest_discover_tests(transmit_fault_tests
    PROPERTIES LABELS fault
    DISCOVERY_TIMEOUT 60)
