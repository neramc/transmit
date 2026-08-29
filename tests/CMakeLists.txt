add_subdirectory(unit)
add_subdirectory(property)

if(TRANSMIT_BUILD_APP)
    add_subdirectory(integration)
    add_subdirectory(ui)
endif()
