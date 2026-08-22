# Headless front end. Also the vehicle for the cross-OS integration checks in
# CI, where no display is available.

qt_add_executable(transmit-cli main_cli.cpp)

target_link_libraries(transmit-cli PRIVATE Transmit::Core Transmit::Warnings)

set_target_properties(transmit-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE)
