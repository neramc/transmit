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
#
# The same for a coverage build, for the same reason with a different cause:
# it is a clang build against whatever Qt the machine has, and Qt 6.4's Wayland
# client dereferences a null pointer on a compositor that offers no text-input
# protocol. Measuring how much of Transmit the tests reach does not need a
# window on a compositor, and a crash in somebody else's client library
# measures nothing.
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize" OR CMAKE_CXX_FLAGS MATCHES "-fprofile-instr-generate")
    set(cases "offscreen")
endif()

# Qt's own Wayland client dies on a compositor like this one before 6.5.
#
# QWaylandInputDevice::textInput() hands back a null pointer when the
# compositor offers no text-input protocol, and QQuickTextInputPrivate::init()
# follows it while the interface's first TextField is being built. Headless
# weston offers no text-input protocol, so the process is gone inside
# libQt6WaylandClient before Transmit gets anywhere near a frame. Measured
# rather than assumed: the same binary segfaults there against Qt 6.4.2 and
# paints a frame against 6.8.1 on the same machine, same compositor.
#
# There is nothing here to fix - the interface cannot stop using text fields -
# and a case that fails for somebody else's reason is a case that stops being
# read. So the Wayland pair is registered only against a Qt that survives it.
# The build job these questions are actually asked in uses 6.8, and it checks
# that all four are still registered, so nothing is quietly lost by this.
if(Qt6_VERSION AND Qt6_VERSION VERSION_LESS 6.5)
    list(REMOVE_ITEM cases "wayland" "wayland-no-buffers")
    message(STATUS
        "Launch tests: the Wayland cases need Qt 6.5 or newer, and this is ${Qt6_VERSION}")
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
