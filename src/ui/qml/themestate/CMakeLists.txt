# The one piece of theme state that changes at runtime, in a module of its own.
#
# It used to live in Transmit.Theme with the palettes and the scales, which
# meant every file that read it imported the module it was itself part of. One
# singleton doing that is tolerated; three of them is a cycle, and the engine
# says so on every start: "Cyclic dependency detected between Colors.qml and
# Sizing.qml". Nothing there actually depends on anything there - they all just
# want this object - so this object is somewhere else now, and the dependency
# graph is a line rather than a ring.
set_source_files_properties(ThemeState.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(transmit_qml_themestate
    URI Transmit.ThemeState
    VERSION 1.0
    STATIC
    QML_FILES ThemeState.qml
    RESOURCE_PREFIX /qt/qml)
