add_subdirectory(unit)
add_subdirectory(security)
add_subdirectory(property)
add_subdirectory(fuzz)
add_subdirectory(fault)

if(TRANSMIT_BUILD_APP)
    add_subdirectory(integration)
    add_subdirectory(ui)
    add_subdirectory(launch)
endif()

if(TRANSMIT_BUILD_BENCH)
    add_subdirectory(bench)
endif()

# The version is data, spread across nine files, and is checked as data. A
# packaging recipe left on the old number is only ever found by whoever builds
# that package, which is not us.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME Version.consistent
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/scripts/version.py" --check)
    set_tests_properties(Version.consistent PROPERTIES LABELS quality TIMEOUT 60)

    # The same for the small things nobody notices until they are the problem:
    # a source file in no build, a test that is never run, a link that points
    # at a renamed file.
    # A translation whose placeholders do not match its source is a sentence
    # that will say something the code never meant, and nothing about the file
    # looks wrong.
    add_test(NAME Translations.consistent
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/scripts/check-translations.py")
    set_tests_properties(Translations.consistent PROPERTIES LABELS quality TIMEOUT 60)

    # And every string a person reads has to be able to have a translation in
    # the first place.
    add_test(NAME Translations.reachable
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/scripts/check-translatable.py")
    set_tests_properties(Translations.reachable PROPERTIES
        LABELS quality TIMEOUT 60
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # The coverage gate decides whether a change may land, so the rules it
    # decides by are themselves tested. Both of them were wrong at once and the
    # symptom was a failing Coverage job, which reads like untested code rather
    # than like a base commit a force-push had orphaned.
    add_test(NAME Coverage.rules
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/scripts/coverage_report.py" --self-test)
    set_tests_properties(Coverage.rules PROPERTIES LABELS quality TIMEOUT 60)

    add_test(NAME Tree.tidy
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/scripts/check-hygiene.py")
    set_tests_properties(Tree.tidy PROPERTIES
        LABELS quality TIMEOUT 120
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
