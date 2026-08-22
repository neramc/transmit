# The interface, driven for real. QML is only checked when it runs: a binding
# to a property that does not exist compiles perfectly and then paints nothing,
# so this loads the actual window and treats any QML warning as a failure.

# RestoreUndoTest drives the controllers rather than the QML, but it needs the
# same application layer, so it is built here rather than in integration/.
foreach(suite UiSmoke RestoreUndo)
    add_executable(transmit_${suite}_test ${suite}Test.cpp)
    set_target_properties(transmit_${suite}_test PROPERTIES AUTOMOC ON)
    add_test(NAME ${suite} COMMAND transmit_${suite}_test)
    set_tests_properties(${suite} PROPERTIES
        LABELS integration
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

target_link_libraries(transmit_RestoreUndo_test PRIVATE
    Transmit::Core Transmit::Warnings
    Qt6::Test Qt6::Gui Qt6::Qml
    transmit_backendplugin)

target_link_libraries(transmit_UiSmoke_test PRIVATE
    Transmit::Core Transmit::Warnings
    Qt6::Test Qt6::Quick Qt6::QuickControls2 Qt6::Gui
    transmit_qml_mainplugin
    transmit_qml_themeplugin
    transmit_qml_componentsplugin
    transmit_qml_dialogsplugin
    transmit_qml_pagesplugin
    transmit_qml_layoutsplugin
    transmit_backendplugin)

# Static QML modules register their types only once their plugins are imported.
qt_import_qml_plugins(transmit_UiSmoke_test)
