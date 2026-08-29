add_subdirectory(unit)
add_subdirectory(property)
add_subdirectory(fuzz)
add_subdirectory(fault)

if(TRANSMIT_BUILD_APP)
    add_subdirectory(integration)
    add_subdirectory(ui)
endif()

if(TRANSMIT_BUILD_BENCH)
    add_subdirectory(bench)
endif()
