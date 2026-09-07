# The rules SECURITY.md states, asked of the code rather than of the reader.
#
# Every property in that file is a claim about what this program will not do.
# A claim nothing checks is a claim that survives the change that breaks it, so
# the ones that can be expressed as a test are expressed here.
#
# Split in two because the configurations differ. Everything that can be asked
# of the format layer alone is asked in every configuration, including the ones
# built without Qt for fuzzing and fault injection; what needs the platform
# layer is only built where that layer exists. Linking the whole suite against
# Transmit::Platform made the Qt-less configurations fail at generate time,
# which took the fuzzers and the fault injection down with it - three jobs
# reporting a missing target rather than the one thing that was wrong.
add_executable(transmit_security_tests ConfinementTest.cpp)

target_link_libraries(transmit_security_tests
    PRIVATE Transmit::Format Transmit::Warnings GTest::gtest GTest::gtest_main)

target_include_directories(transmit_security_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

gtest_discover_tests(transmit_security_tests
    PROPERTIES LABELS security
    DISCOVERY_TIMEOUT 60)

if(TARGET Transmit::Platform)
    add_executable(transmit_system_map_tests SystemMapTest.cpp)

    target_link_libraries(transmit_system_map_tests
        PRIVATE Transmit::Format Transmit::Platform Transmit::Warnings
                GTest::gtest GTest::gtest_main)

    # The table is read from the source tree rather than from the resource, so
    # that a failure names the file somebody has to edit.
    target_compile_definitions(transmit_system_map_tests
        PRIVATE TRANSMIT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    target_include_directories(transmit_system_map_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

    gtest_discover_tests(transmit_system_map_tests
        PROPERTIES LABELS security
        DISCOVERY_TIMEOUT 60)
endif()
