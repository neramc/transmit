#!/usr/bin/env bash
#
# Installs the Linux packages and starts what they installed.
#
# A .deb and a .rpm are not an AppImage. The bundle carries its own Qt; these
# two depend on the distribution's, resolved by the package manager, with the
# QML modules coming from separate packages that nothing links and therefore
# nothing can find automatically. Building one proves it can be built. Only
# installing it and opening a window proves the thing is usable.
#
#   scripts/check-linux-packages.sh BUILD_DIR
#
# The .rpm cannot be installed on a machine that uses dpkg, so it is installed
# in a container instead. Without a container runtime that part says it was
# skipped and why - unless TRANSMIT_PACKAGE_TESTS_REQUIRED is set, which is how
# continuous integration turns a silent skip into a failure.

set -uo pipefail

build="${1:-build-package}"
here="$(cd "$(dirname "$0")/.." && pwd)"

problems=0
note() { echo "  $*"; }
fault() { echo "FAILED: $*" >&2; problems=1; }

find_one() {
    find "$build" -maxdepth 1 -name "$1" -type f | head -1
}

# The Qt this program will actually load, taken from the file its soname
# resolves to: libQt6Core.so.6 is a link to libQt6Core.so.6.4.2.
qt_behind() {
    local line lib
    line=$(ldd "$1" 2>/dev/null | grep -m1 'libQt6Core\.so\.6')
    lib=${line#*=> }
    lib=${lib% (*}
    lib=$(readlink -f "$lib" 2>/dev/null)
    case "$lib" in
        *.so.6.*) echo "${lib##*.so.}" ;;
        *) echo "" ;;
    esac
}

deb=$(find_one '*.deb')
rpm=$(find_one '*.rpm')

# ---------------------------------------------------------------- the .deb ---

echo "== The .deb =="
if [ -z "$deb" ]; then
    fault "no .deb in $build"
else
    note "$deb"
    contents=$(dpkg-deb -c "$deb" | awk '{print $6}')
    for needed in ./usr/bin/transmit ./usr/bin/transmit-cli \
                  ./usr/share/applications/transmit.desktop; do
        if ! printf '%s\n' "$contents" | grep -qx -- "$needed"; then
            fault "the .deb has no $needed"
        fi
    done

    # Worked out by dpkg-shlibdeps rather than written down. An empty list is
    # not a package with no dependencies, it is machinery that did nothing.
    depends=$(dpkg-deb -f "$deb" Depends)
    note "Depends: $depends"
    case "$depends" in
        *libqt6core*) ;;
        *) fault "the .deb does not depend on Qt, so shlibdeps found nothing" ;;
    esac
fi

if [ -n "$deb" ] && command -v dpkg >/dev/null 2>&1; then
    echo "== Installing the .deb =="
    if sudo dpkg -i "$deb" >/dev/null 2>&1 || sudo apt-get -f install -y >/dev/null 2>&1; then
        if [ ! -x /usr/bin/transmit ] || [ ! -x /usr/bin/transmit-cli ]; then
            fault "the .deb installed without putting both programs in /usr/bin"
        else
            said=$(/usr/bin/transmit-cli --version)
            declared=$("$here/scripts/version.py" --print)
            note "the installed tool says: $said"
            case "$said" in
                *"$declared"*) ;;
                *) fault "the installed tool says '$said', not $declared" ;;
            esac

            # Qt's own Wayland client dies on a compositor with no text-input
            # protocol before 6.5: QWaylandInputDevice::textInput() hands back
            # a null pointer and QQuickTextInputPrivate::init() follows it
            # while the first text field is being built. A package linked
            # against such a Qt fails here for a reason that is not this
            # program's, and a check that fails for somebody else's reason
            # stops being read. The same rule the launch tests use.
            modes="offscreen xcb wayland"
            qt=$(qt_behind /usr/bin/transmit)
            case "$qt" in
                6.[0-4].*)
                    modes="offscreen xcb"
                    note "Qt $qt is behind this package, so the Wayland case is left out"
                    note "  (Qt's Wayland client crashes on a headless compositor before 6.5)"
                    ;;
                "") note "could not tell which Qt this links, so every case is run" ;;
                *) note "Qt $qt is behind this package" ;;
            esac

            for mode in $modes; do
                echo "-- the installed program: $mode --"
                if ! bash "$here/tests/launch/launch-test.sh" \
                        --binary /usr/bin/transmit --mode "$mode"; then
                    fault "the installed program did not start under $mode"
                fi
            done
        fi

        sudo dpkg -r transmit >/dev/null 2>&1
        if [ -e /usr/bin/transmit ]; then
            fault "removing the package left /usr/bin/transmit behind"
        fi
    else
        fault "the .deb would not install"
    fi
fi

# ---------------------------------------------------------------- the .rpm ---

echo "== The .rpm =="
if [ -z "$rpm" ]; then
    fault "no .rpm in $build"
else
    note "$rpm"
    if command -v rpm >/dev/null 2>&1; then
        requires=$(rpm -qp --requires "$rpm" 2>/dev/null)
        printf '%s\n' "$requires" | sed 's/^/    /' | head -20
        case "$requires" in
            *libQt6Core*) ;;
            *) fault "the .rpm does not require Qt, so its dependencies are wrong" ;;
        esac
    else
        note "rpm is not installed here, so the package was not inspected"
    fi

    runtime=""
    for candidate in docker podman; do
        if command -v "$candidate" >/dev/null 2>&1 && "$candidate" info >/dev/null 2>&1; then
            runtime="$candidate"
            break
        fi
    done

    if [ -z "$runtime" ]; then
        if [ "${TRANSMIT_PACKAGE_TESTS_REQUIRED:-0}" != "0" ]; then
            fault "no container runtime, and this machine was told it must have one"
        else
            note "SKIPPED: installing the .rpm needs a container runtime and there is none"
        fi
    else
        echo "== Installing the .rpm on Fedora with $runtime =="
        if ! "$runtime" run --rm -v "$here:/work" -w /work fedora:40 \
                bash /work/scripts/inside-fedora.sh "/work/$rpm"; then
            fault "the .rpm did not install and start on Fedora"
        fi
    fi
fi

echo
if [ "$problems" -ne 0 ]; then
    echo "The packages are not usable as they are." >&2
    exit 1
fi
echo "Both packages install and the program starts from each."
