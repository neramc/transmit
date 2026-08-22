# Integration tests exercise the services against a real filesystem, including
# the cross-OS translation that the unit tests cannot reach from the format
# layer alone.

foreach(suite ContinuityRoundTrip PathRewrite SecretStore)
    add_executable(transmit_${suite}_test ${suite}Test.cpp)
    target_link_libraries(transmit_${suite}_test
        PRIVATE Transmit::Core Transmit::Warnings Qt6::Test SQLite::SQLite3)
    set_target_properties(transmit_${suite}_test PROPERTIES AUTOMOC ON)

    add_test(NAME ${suite} COMMAND transmit_${suite}_test)
    set_tests_properties(${suite} PROPERTIES LABELS integration)
endforeach()
