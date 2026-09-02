add_subdirectory(unit)
add_subdirectory(property)
add_subdirectory(fuzz)
add_subdirectory(fault)

if(TRANSMIT_BUILD_APP)
    add_subdirectory(integration)
    add_subdirectory(ui)
    add_subdirectory(launch)
endif()

if(TRANSMIT_BUILD_BENCH)
    add_subdirectory(bench)
endif()
