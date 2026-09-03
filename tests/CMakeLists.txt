add_subdirectory(unit)
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
    add_test(NAME Tree.tidy
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/scripts/check-hygiene.py")
    set_tests_properties(Tree.tidy PROPERTIES
        LABELS quality TIMEOUT 120
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
