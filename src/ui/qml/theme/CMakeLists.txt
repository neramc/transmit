# The design system.
#
# It is split by concern rather than kept in one object: a component that only
# needs a margin should not have to reason about the palette, and the split
# makes it obvious at the import site what a file actually depends on.
#
# ThemeState holds the only value that changes at runtime; everything else is
# constants, or reads ThemeState. That keeps the dependencies acyclic:
#
#   ThemeState <- Colors <- Elevation
#   Typography, Spacing, Radius, Sizing, Motion   (no dependencies)

set(TRANSMIT_THEME_SINGLETONS
    ThemeState.qml
    Colors.qml
    Typography.qml
    Spacing.qml
    Radius.qml
    Sizing.qml
    Elevation.qml
    Motion.qml)

# Must be set before the module is declared: qt_add_qml_module reads this
# property when it writes the qmldir, and a singleton without the keyword
# resolves to undefined everywhere it is used.
set_source_files_properties(${TRANSMIT_THEME_SINGLETONS}
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(transmit_qml_theme
    URI Transmit.Theme
    VERSION 1.0
    STATIC
    QML_FILES ${TRANSMIT_THEME_SINGLETONS}
    RESOURCE_PREFIX /qt/qml)
