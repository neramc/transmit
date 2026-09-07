# Distribution packages built from the same install rules as everything else.
#
# A .deb and a .rpm are what somebody on a Debian or Fedora machine expects to
# be handed, and building them from CPack means they carry exactly what
# `cmake --install` lays down - not a second, hand-written list of files that
# drifts away from the first one the moment anybody adds a resource.
#
# The generator is chosen on the command line (`cpack -G DEB`), so this file
# describes the packages without deciding which are built.

if(NOT UNIX OR APPLE)
    return()
endif()

set(CPACK_PACKAGE_NAME "transmit")
set(CPACK_PACKAGE_VENDOR "Transmit contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Move a computer's environment to one running another operating system")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/neramc/transmit")
set(CPACK_PACKAGE_CONTACT "Transmit contributors <noreply@example.invalid>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.md")
set(CPACK_PACKAGE_FILE_NAME
    "transmit_${PROJECT_VERSION}_${CMAKE_SYSTEM_PROCESSOR}")

# Read by both generators. Kept short deliberately: the long description lives
# in the packaging recipes, which are read by distribution maintainers, and a
# summary repeated in four places is a summary that will disagree with itself.
set(CPACK_PACKAGE_DESCRIPTION
    "Transmit captures your files, the data and settings your programs keep,\n"
    "your desktop preferences and the list of what you have installed;\n"
    "compresses all of it onto removable media; and restores it on a machine\n"
    "running a different operating system.")
string(REPLACE ";" "" CPACK_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")

# --- Debian ------------------------------------------------------------------

set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# Worked out from the binaries rather than written down, because a list of
# libraries maintained by hand is a list that is wrong after the next
# dependency change. Everything the program links is found by dpkg-shlibdeps.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# QML modules are loaded at run time, so nothing links them and dpkg-shlibdeps
# cannot see them. They are Depends and not Recommends because without them the
# program does not start at all: QtQuick.Controls' ApplicationWindow is built on
# QtQuick.Templates, and a missing templates plugin makes the whole window
# unavailable. The package installed cleanly and then said "Type
# ApplicationWindow unavailable" on every launch - which is the shape of
# failure a package that only Recommends what it needs produces.
#
# qtquick-templates was not even in the list. It is imported by nothing here;
# it is what QtQuick.Controls imports, and that is exactly the kind of
# dependency a list written from a file's own import statements misses.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-qtquick, qml6-module-qtquick-templates, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-dialogs, qml6-module-qtqml-workerscript")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libsecret-tools")

# --- RPM ---------------------------------------------------------------------

set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")

# The same runtime modules, under the names Fedora gives them. rpm's automatic
# requirements find the shared libraries the binary links and stop there, so
# the QML plugins - dlopen'd, named in no ELF header - have to be said.
# qt6-qtdeclarative carries QtQuick, QtQml and, since Qt 6.2, Quick Controls
# and its templates; qt6-qtbase-gui carries the platform plugins without which
# there is no window to put them in.
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, qt6-qtbase-gui")

# Directories the base system already owns. Claiming them makes the package
# conflict with filesystem, and every other package that installs an icon.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications
    /usr/share/icons
    /usr/share/icons/hicolor
    /usr/share/icons/hicolor/scalable
    /usr/share/icons/hicolor/scalable/apps
    /usr/share/metainfo)

include(CPack)
