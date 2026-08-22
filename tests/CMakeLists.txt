add_subdirectory(unit)

if(TRANSMIT_BUILD_APP)
    add_subdirectory(integration)
    add_subdirectory(ui)
endif()
