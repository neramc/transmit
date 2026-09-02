# The interface, launched the way a person launches it.
#
# These are the only tests that run the built executable rather than linking
# its pieces into a test process, and the only ones that involve a real
# platform plugin and a real graphics stack. They exist because version 0.1.0
# shipped a bundle that aborted on every Wayland desktop while the whole suite
# stayed green: nothing here had ever started the program.
#
# Unix only. The script drives Xvfb and weston, and the two platforms that
# would need an equivalent - Windows and macOS - have no headless session to
# drive; the packaged smoke test in the release workflow covers them instead.
if(NOT UNIX OR APPLE)
    return()
endif()

set(launcher "${CMAKE_CURRENT_SOURCE_DIR}/launch-test.sh")

# mode, extra arguments, expectation
set(cases
    "offscreen"                  # no windowing system: proves the fallback path
    "xcb"                        # a real X server, real OpenGL through llvmpipe
    "wayland"                    # a real compositor
    "wayland-no-buffers")        # the exact shape of the 0.1.0 crash

# Under a sanitiser, only the case that loads no third-party platform plugin.
#
# The xcb and Wayland plugins are Qt's, built without instrumentation, and
# running them under AddressSanitizer reports faults inside them that this
# project can neither fix nor silence honestly - a heap overflow while
# QXcbConnection reads the X server's setup, a null dereference in the Wayland
# input handling on a compositor with no text-input protocol. Neither is
# Transmit's, and a check that fails for somebody else's reason stops being
# read. The offscreen plugin does almost nothing, so it stays: it is the one
# that walks the whole of main() and the QML engine under the sanitiser.
#
# The Linux build job runs all four, uninstrumented, which is where the
# question these ask actually belongs.
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize")
    set(cases "offscreen")
endif()

foreach(case ${cases})
    set(arguments --binary "$<TARGET_FILE:transmit>")
    if(case STREQUAL "wayland-no-buffers")
        list(APPEND arguments --mode wayland --without-buffer-integration
                              --expect-software)
    elseif(case STREQUAL "offscreen")
        # No windowing system offers OpenGL, so this is the fallback taken on
        # purpose - and the cheapest place to notice it has stopped working.
        list(APPEND arguments --mode offscreen --expect-software)
    elseif(case STREQUAL "xcb")
        # Software drawing here would mean the interface had quietly stopped
        # using the graphics card on machines that have one.
        list(APPEND arguments --mode xcb --expect-accelerated)
    else()
        list(APPEND arguments --mode ${case})
    endif()

    add_test(NAME Launch.${case} COMMAND bash "${launcher}" ${arguments})
    set_tests_properties(Launch.${case} PROPERTIES
        LABELS launch
        SKIP_RETURN_CODE 77
        TIMEOUT 180)
endforeach()
