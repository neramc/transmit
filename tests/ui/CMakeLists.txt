# The interface, driven for real. QML is only checked when it runs: a binding
# to a property that does not exist compiles perfectly and then paints nothing,
# so this loads the actual window and treats any QML warning as a failure.

add_executable(transmit_UiSmoke_test UiSmokeTest.cpp)

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

set_target_properties(transmit_UiSmoke_test PROPERTIES AUTOMOC ON)

# Static QML modules register their types only once their plugins are imported.
qt_import_qml_plugins(transmit_UiSmoke_test)

add_test(NAME UiSmoke COMMAND transmit_UiSmoke_test)
set_tests_properties(UiSmoke PROPERTIES
    LABELS integration
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
