# The rules SECURITY.md states, asked of the code rather than of the reader.
#
# Every property in that file is a claim about what this program will not do.
# A claim nothing checks is a claim that survives the change that breaks it, so
# the ones that can be expressed as a test are expressed here.
add_executable(transmit_security_tests
    ConfinementTest.cpp)

target_link_libraries(transmit_security_tests
    PRIVATE Transmit::Format Transmit::Warnings GTest::gtest GTest::gtest_main)

target_include_directories(transmit_security_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

gtest_discover_tests(transmit_security_tests
    PROPERTIES LABELS security
    DISCOVERY_TIMEOUT 60)
