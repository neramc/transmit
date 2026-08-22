# Integration tests exercise the services against a real filesystem, including
# the cross-OS translation that the unit tests cannot reach from the format
# layer alone.

add_executable(transmit_integration_tests
    ContinuityRoundTripTest.cpp)

target_link_libraries(transmit_integration_tests
    PRIVATE Transmit::Core Transmit::Warnings Qt6::Test)

set_target_properties(transmit_integration_tests PROPERTIES AUTOMOC ON)

add_test(NAME continuity_round_trip COMMAND transmit_integration_tests)
set_tests_properties(continuity_round_trip PROPERTIES LABELS integration)
