# Shared utilities used by both the platform layer and the rest of core.
# Kept as its own target so neither library ends up with a duplicate copy.

add_library(transmit_common STATIC
    Logging.cpp
    Conversions.cpp
    StageTimer.cpp)

add_library(Transmit::Common ALIAS transmit_common)

target_include_directories(transmit_common PUBLIC "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(transmit_common
    PUBLIC Transmit::Format Transmit::Warnings Qt6::Core)
