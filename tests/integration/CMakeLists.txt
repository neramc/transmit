# Integration tests exercise the services against a real filesystem, including
# the cross-OS translation that the unit tests cannot reach from the format
# layer alone.

foreach(suite ContinuityRoundTrip PathRewrite SecretStore Catalog SelectionCodec
              VerifyService StageTimer ResumeCapture Eject)
    add_executable(transmit_${suite}_test ${suite}Test.cpp)
    target_link_libraries(transmit_${suite}_test
        PRIVATE Transmit::Core Transmit::Warnings Qt6::Test SQLite::SQLite3)
    set_target_properties(transmit_${suite}_test PROPERTIES AUTOMOC ON)

    add_test(NAME ${suite} COMMAND transmit_${suite}_test)
    set_tests_properties(${suite} PROPERTIES LABELS integration)
endforeach()

# The catalog exactly as it was before the migration to schema 2, so the two
# readings can be compared field by field. It is a test input rather than
# something the application ships, so it is compiled into this binary alone.
qt_add_resources(transmit_Catalog_test "catalog_fixtures"
    PREFIX "/fixtures"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/app-catalog-v1.json")
