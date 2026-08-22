# The design system. Everything visual resolves through this singleton, so it
# is the one module every other QML module depends on.

# Must be set before the module is declared: qt_add_qml_module reads this
# property when it writes the qmldir, and a Theme without the singleton keyword
# resolves to undefined everywhere it is used.
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(transmit_qml_theme
    URI Transmit.Theme
    VERSION 1.0
    STATIC
    QML_FILES Theme.qml
    RESOURCE_PREFIX /qt/qml)
