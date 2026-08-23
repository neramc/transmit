# Headless front end. Also the vehicle for the cross-OS integration checks in
# CI, where no display is available.

qt_add_executable(transmit-cli main_cli.cpp Terminal.cpp)

target_link_libraries(transmit-cli PRIVATE Transmit::Core Transmit::Warnings)

set_target_properties(transmit-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE)

# Installed from here rather than next to the interface's own rule: src/app is
# configured first, so an "if(TARGET transmit-cli)" over there is asking about
# a target that does not exist yet. It answered no, quietly, and every package
# built from `cmake --install` shipped without the command line tool.
include(GNUInstallDirs)
install(TARGETS transmit-cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
