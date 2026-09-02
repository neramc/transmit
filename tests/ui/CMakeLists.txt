# The interface, driven for real. QML is only checked when it runs: a binding
# to a property that does not exist compiles perfectly and then paints nothing,
# so this loads the actual window and treats any QML warning as a failure.

# RestoreUndoTest, AppSelectionTest and CarryOnTest drive the controllers
# rather than the QML, but they need the same application layer, so they are
# built here rather than in integration/.
foreach(suite UiSmoke LayoutConformance RestoreUndo AppSelection CarryOn FolderSelection)
    add_executable(transmit_${suite}_test ${suite}Test.cpp)
    set_target_properties(transmit_${suite}_test PROPERTIES AUTOMOC ON)
    add_test(NAME ${suite} COMMAND transmit_${suite}_test)
    # No update check from a test: it would reach somebody else's server on
    # every run, and a suite that needs the internet to pass is a suite that
    # fails for reasons that have nothing to do with the change.
    set_tests_properties(${suite} PROPERTIES
        LABELS integration
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TRANSMIT_NO_UPDATE_CHECK=1")
endforeach()

foreach(suite RestoreUndo AppSelection CarryOn FolderSelection)
    target_link_libraries(transmit_${suite}_test PRIVATE
        Transmit::Core Transmit::Warnings
        Qt6::Test Qt6::Gui Qt6::Qml
        transmit_backendplugin)
endforeach()

# Both interface suites load the real window, so they need the same modules.
foreach(suite UiSmoke LayoutConformance)
    target_link_libraries(transmit_${suite}_test PRIVATE
        Transmit::Core Transmit::Warnings
        Qt6::Test Qt6::Quick Qt6::QuickControls2 Qt6::Gui
        transmit_qml_mainplugin
        transmit_qml_themeplugin
        transmit_qml_componentsplugin
        transmit_qml_dialogsplugin
        transmit_qml_pagesplugin
        transmit_qml_layoutsplugin
        transmit_backendplugin)

    # Static QML modules register their types only once their plugins are
    # imported.
    qt_import_qml_plugins(transmit_${suite}_test)
endforeach()
